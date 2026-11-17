/*============================================================================================================================================
                                                           FLUIDVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidViewportHost: renders live Hero+World volumetric preview, embers, 4-tool rail & playback bar

#include "Engine/Workspaces/Fluid/FluidViewportHost.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

void FluidViewportHost::Render(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer,
    float                  Opacity) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg.ScaledAlpha(Opacity), 22.0f);
    Surface.StrokeRectangle(Bounds, C.HairlineStrong.ScaledAlpha(Opacity), 22.0f, 1.0f);

    const Rectangle2D TopBar { Bounds.X + 12.0f, Bounds.Y + 10.0f, Bounds.Width - 24.0f, 30.0f };
    const Rectangle2D BottomBar { Bounds.X + 12.0f, Bounds.Bottom() - 44.0f, Bounds.Width - 24.0f, 34.0f };
    LastSurfaceBounds = {
        Bounds.X + 12.0f,
        TopBar.Bottom() + 8.0f,
        Bounds.Width - 24.0f,
        std::max(120.0f, BottomBar.Y - (TopBar.Bottom() + 16.0f)),
    };

    // 1. Top Tool & Channel Bar
    float TX = TopBar.X;
    struct ToolSpec { FluidViewportTool Tool; const char* Label; };
    constexpr std::array<ToolSpec, 4> Tools {{
        { FluidViewportTool::OrbitCamera,   "Orbit" },
        { FluidViewportTool::PyroBrush,     "Brush" },
        { FluidViewportTool::DetonateBlast, "Blast" },
        { FluidViewportTool::CrossSection,  "Slice" },
    }};
    for (const auto& T : Tools)
    {
        const ButtonTone Tone = (Session.ActiveTool == T.Tool) ? ButtonTone::Primary : ButtonTone::Secondary;
        if (CapsuleButton::Construct(Surface, { TX, TopBar.Y + 2.0f, 64.0f, 25.0f }, ButtonDescriptor{ T.Label, Tone, 10.5f, 12.0f }, Pointer).Pressed)
        {
            Session.ActiveTool = T.Tool;
            if (T.Tool == FluidViewportTool::CrossSection)
            {
                auto& Sh = Session.AccessSimulation().AccessDocument().Shading;
                Sh.RenderChannel = (Sh.RenderChannel == SimulationSpace::FluidSpace::FluidRenderChannel::CrossSection)
                    ? SimulationSpace::FluidSpace::FluidRenderChannel::LitVolume
                    : SimulationSpace::FluidSpace::FluidRenderChannel::CrossSection;
            }
        }
        TX += 70.0f;
    }

    // 2. Interactive Orbit / Brush / Detonate inside Viewport Surface
    const bool InsideView = LastSurfaceBounds.Contains({ Pointer.X, Pointer.Y });
    if (InsideView && Pointer.Pressed)
    {
        if (Session.ActiveTool == FluidViewportTool::DetonateBlast)
        {
            Session.TriggerBlastNow();
        }
        else if (Session.ActiveTool == FluidViewportTool::PyroBrush)
        {
            const float NX = (Pointer.X - LastSurfaceBounds.Center().X) / std::max(1.0f, LastSurfaceBounds.Width);
            const float NZ = (LastSurfaceBounds.Bottom() - Pointer.Y) / std::max(1.0f, LastSurfaceBounds.Height);
            Session.AccessSimulation().ApplyPyroBrush(
                { NX * 1.4f, 0.0f, 0.35f + NZ * 1.8f },
                { 0.0f, 0.0f, 4.5f },
                0.18f,
                1.4f);
        }
        else
        {
            DraggingOrbit  = true;
            LastPointerPos = { Pointer.X, Pointer.Y };
        }
    }
    if (DraggingOrbit && Pointer.Down)
    {
        const float DX = Pointer.X - LastPointerPos.X;
        const float DY = Pointer.Y - LastPointerPos.Y;
        Session.Camera.YawRadians   -= DX * 0.01f;
        Session.Camera.PitchRadians = std::clamp(Session.Camera.PitchRadians + DY * 0.008f, -0.45f, 1.35f);
        LastPointerPos = { Pointer.X, Pointer.Y };
    }
    else if (!Pointer.Down)
    {
        DraggingOrbit = false;
    }
    if (InsideView)
    {
        const float Wheel = Surface.QueryMouseWheelY();
        if (std::abs(Wheel) > 1.0e-3f)
        {
            Session.Camera.DistanceMeters = std::clamp(Session.Camera.DistanceMeters - Wheel * 0.35f, 1.5f, 14.0f);
        }
    }

    // 3. Render Volumetric Raymarched Framebuffer cleanly into LastSurfaceBounds
    Surface.PushClip(LastSurfaceBounds);
    Surface.FillRectangle(LastSurfaceBounds, { 12u, 14u, 19u, 255u }, 10.0f);

    Session.RenderPreviewFramebuffer(96u, 72u);
    if (Session.PreviewWidthPx > 0u && Session.PreviewHeightPx > 0u &&
        Session.PreviewRgba8.size() >= static_cast<std::size_t>(Session.PreviewWidthPx) * Session.PreviewHeightPx * 4u)
    {
        const float CellW = LastSurfaceBounds.Width / static_cast<float>(Session.PreviewWidthPx);
        const float CellH = LastSurfaceBounds.Height / static_cast<float>(Session.PreviewHeightPx);
        for (uint32_t PY = 0u; PY < Session.PreviewHeightPx; ++PY)
        {
            for (uint32_t PX = 0u; PX < Session.PreviewWidthPx; ++PX)
            {
                const std::size_t Off = (static_cast<std::size_t>(PY) * Session.PreviewWidthPx + PX) * 4u;
                const ColorRgba PxColor {
                    Session.PreviewRgba8[Off + 0u],
                    Session.PreviewRgba8[Off + 1u],
                    Session.PreviewRgba8[Off + 2u],
                    255u,
                };
                Surface.FillRectangle(
                    {
                        LastSurfaceBounds.X + static_cast<float>(PX) * CellW,
                        LastSurfaceBounds.Y + static_cast<float>(PY) * CellH,
                        CellW + 0.5f,
                        CellH + 0.5f,
                    },
                    PxColor,
                    0.0f);
            }
        }
    }

    char Hud[128] {};
    const auto& Hero = Session.InspectSimulation().InspectHeroGrid();
    std::snprintf(Hud, sizeof(Hud), "Hero %u^3 (Z %.1f..%.1fm)  |  Embers %zu  |  GI Lights %zu",
        Hero.Resolution, Hero.BoxMinWorld.Z, Hero.BoxMaxWorld.Z,
        Session.InspectSimulation().InspectParticleCoupler().InspectEmberSlots().size(),
        Session.InspectSimulation().InspectEmissiveLights().size());
    Surface.Text({ LastSurfaceBounds.X + 10.0f, LastSurfaceBounds.Y + 8.0f }, 10.5f, C.TextPrimary, Hud);
    Surface.PopClip();

    // 4. Bottom Playback & Detonation Strip
    float BX = BottomBar.X;
    auto& Domain = Session.AccessSimulation().AccessDocument().Domain;
    const char* PlayLabel = Domain.Paused ? "Play" : "Pause";
    if (CapsuleButton::Construct(Surface, { BX, BottomBar.Y + 4.0f, 68.0f, 26.0f }, ButtonDescriptor{ PlayLabel, ButtonTone::Primary, 10.5f, 12.0f }, Pointer).Pressed)
    {
        Domain.Paused = !Domain.Paused;
    }
    BX += 74.0f;

    if (CapsuleButton::Construct(Surface, { BX, BottomBar.Y + 4.0f, 64.0f, 26.0f }, ButtonDescriptor{ "Step", ButtonTone::Secondary, 10.5f, 12.0f }, Pointer).Pressed)
    {
        const bool WasPaused = Domain.Paused;
        Domain.Paused = false;
        Session.StepSimulation(1.0 / 60.0);
        Domain.Paused = WasPaused;
    }
    BX += 70.0f;

    if (CapsuleButton::Construct(Surface, { BX, BottomBar.Y + 4.0f, 68.0f, 26.0f }, ButtonDescriptor{ "Reset", ButtonTone::Secondary, 10.5f, 12.0f }, Pointer).Pressed)
    {
        Session.TriggerReset();
    }
    BX += 74.0f;

    if (CapsuleButton::Construct(Surface, { BX, BottomBar.Y + 4.0f, 82.0f, 26.0f }, ButtonDescriptor{ "Detonate!", ButtonTone::Primary, 10.5f, 12.0f }, Pointer).Pressed)
    {
        Session.TriggerBlastNow();
    }
}

} // namespace Frontier::Workspaces
