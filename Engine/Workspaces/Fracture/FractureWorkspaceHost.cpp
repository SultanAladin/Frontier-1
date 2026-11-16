/*============================================================================================================================================
                                                         FRACTUREWORKSPACEHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureWorkspaceHost: 56px obsidian Studio Header, Outliner, OpenPBR Viewport, Inspector & Status Bar

#include "Engine/Workspaces/Fracture/FractureWorkspaceHost.h"

#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <cstdio>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace ParticleAppearance;

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
    Viewport.InvalidateRenderCache();
}

void FractureWorkspaceHost::AttachContentBrowserPanel(ContentBrowserPanel* TargetPanel) noexcept
{
    BoundBrowser = TargetPanel;
    if (BoundBrowser != nullptr)
    {
        BoundBrowser->ConfigureWorkspaceDomain(WorkspaceCategory::Simulation);
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

    // Brand mark: "frontier." + FRACTURE STUDIO pill
    Surface.Text({ X, CY - 10.5f }, 19.0f, C.TextPrimary, "frontier");
    const float FrontierW = Surface.MeasureTextWidth(19.0f, "frontier");
    Surface.FillCircle({ X + FrontierW + 3.0f, CY + 4.5f }, 2.2f, C.WarmAccent);
    X += FrontierW + 16.0f;

    const Rectangle2D BadgePill { X, CY - 10.5f, 108.0f, 21.0f };
    Surface.StrokeRectangle(BadgePill, C.HairlineStrong, 10.5f, 1.0f);
    Surface.TextCentered(BadgePill, 8.2f, C.TextMuted, "FRACTURE STUDIO");
    X = BadgePill.Right() + 20.0f;

    // 6 Preset tabs
    const auto& Presets = FractureWorkspaceSession::InspectPresets();
    const float ActionReserve = 128.0f;
    for (uint32_t I = 0u; I < FractureWorkspaceSession::PresetCount; ++I)
    {
        const char* Title = Presets[I].Title;
        const float TabW  = std::max(84.0f, Surface.MeasureTextWidth(9.6f, Title) + 20.0f);
        if (X + TabW > HeaderRect.Right() - ActionReserve)
        {
            break;
        }
        const Rectangle2D TabRect { X, CY - 14.0f, TabW, 28.0f };
        const bool Active  = (Session.ActivePresetIndex == I);
        const bool Hovered = ControlAppearance::QueryPointerOver(TabRect, Pointer);
        Surface.FillRectangle(TabRect, Active ? C.CardElevated : (Hovered ? C.CardBg : C.WellBg), 14.0f);
        Surface.StrokeRectangle(TabRect, Active ? C.WarmAccent : C.Hairline, 14.0f, 1.0f);
        Surface.TextCentered(TabRect, 9.6f, Active ? C.TextPrimary : C.TextSoft, Title);
        if (Hovered && Pointer.Pressed)
        {
            Session.ApplyPreset(I);
        }
        X += TabW + 6.0f;
    }

    // Right primary CTA: "Fracture Solid"
    const Rectangle2D ActionRect { HeaderRect.Right() - 122.0f, CY - 14.5f, 104.0f, 29.0f };
    const bool ActionHover = ControlAppearance::QueryPointerOver(ActionRect, Pointer);
    Surface.FillRectangle(ActionRect, ActionHover ? C.HighlightBg : C.WarmAccent, 14.5f);
    Surface.TextCentered(ActionRect, 10.0f, C.HighlightInk, "Fracture Solid");
    if (ActionHover && Pointer.Pressed)
    {
        Session.ExecuteFracturePartitionOnly();
        if (Session.ExecutionPhase != ExecutionStripPhase::Edit)
        {
            Session.StartOrResetSimulation(Session.Playing);
        }
    }
}

void FractureWorkspaceHost::RenderStatusBar(PixelSpace& Surface, Rectangle2D StatusRect) const noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(StatusRect, C.CanvasBg, 0.0f);
    Surface.StrokeLine({ StatusRect.X, StatusRect.Y }, { StatusRect.Right(), StatusRect.Y }, C.Hairline, 1.0f);

    const auto& R   = Session.PartitionResult.Receipt;
    const auto  Tel = Session.SimRunner.InspectTelemetry();
    char StatusText[180] {};
    std::snprintf(
        StatusText,
        sizeof(StatusText),
        "RHR +Z Up · JoltPhysics v5.6.0 · OpenPBR Direct Studio · %u fragments · %u/%u bonds · Drift %.1e · %.2f ms",
        R.FragmentCount,
        Session.HasSimulatedData ? Tel.IntactBondCount : R.IntactBondCount,
        R.BondCount,
        R.VolumeDriftRatio,
        R.ComputeDurationMs);
    Surface.Text({ StatusRect.X + 18.0f, StatusRect.Y + 6.0f }, 9.2f, C.TextMuted, StatusText);
}

void FractureWorkspaceHost::RenderStudio(
    PixelSpace&                       Surface,
    Rectangle2D                       WindowBounds,
    const ContentBrowserPointerInput& PointerInput,
    double                            DeltaSeconds) noexcept
{
    Session.AdvanceFrame(DeltaSeconds);

    const auto C = QueryStudioColors();
    Surface.FillRectangle(WindowBounds, C.CanvasBg, 0.0f);

    const ControlPointer Pointer {
        PointerInput.CursorX,
        PointerInput.CursorY,
        PointerInput.HeldDown,
        PointerInput.Pressed,
        PointerInput.Released,
        true
    };

    const float HeaderH = 56.0f;
    const float StatusH = 26.0f;
    const float Margin  = 10.0f;
    const float Gap     = 10.0f;

    const Rectangle2D HeaderRect { WindowBounds.X, WindowBounds.Y, WindowBounds.Width, HeaderH };
    const Rectangle2D StatusRect { WindowBounds.X, WindowBounds.Bottom() - StatusH, WindowBounds.Width, StatusH };
    const float BodyY = HeaderRect.Bottom() + 4.0f;
    const float BodyH = std::max(240.0f, StatusRect.Y - BodyY - Margin);

    const float OutlinerW  = std::clamp(WindowBounds.Width * 0.185f, 220.0f, 272.0f);
    const float InspectorW = std::clamp(WindowBounds.Width * 0.220f, 268.0f, 324.0f);
    const float ViewportW  = std::max(280.0f, WindowBounds.Width - Margin * 2.0f - OutlinerW - InspectorW - Gap * 2.0f);

    const Rectangle2D OutlinerRect  { WindowBounds.X + Margin, BodyY, OutlinerW, BodyH };
    const Rectangle2D ViewportRect  { OutlinerRect.Right() + Gap, BodyY, ViewportW, BodyH };
    const Rectangle2D InspectorRect { ViewportRect.Right() + Gap, BodyY, InspectorW, BodyH };

    RenderStudioHeader(Surface, HeaderRect, Pointer);
    Outliner.Render(Surface, OutlinerRect, Session, Pointer, PointerInput.WheelDeltaY, 1.0f);
    Viewport.Render(Surface, ViewportRect, Session, Pointer, PointerInput.WheelDeltaY, 1.0f);
    Inspector.Render(Surface, InspectorRect, Session, Pointer, 1.0f);
    RenderStatusBar(Surface, StatusRect);
}

} // namespace Frontier::Workspaces::Fracture
