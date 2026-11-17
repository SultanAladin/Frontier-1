/*============================================================================================================================================
                                                          FLUIDOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidOutlinerPanel: scene hierarchy selection and live adaptive ceiling / grid telemetry

#include "Engine/Workspaces/Fluid/FluidOutlinerPanel.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

void FluidOutlinerPanel::Render(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer,
    float                  Opacity) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg.ScaledAlpha(Opacity), 22.0f);
    Surface.StrokeRectangle(Bounds, C.HairlineStrong.ScaledAlpha(Opacity), 22.0f, 1.0f);

    float Y = Bounds.Y + 14.0f;
    const float X = Bounds.X + 14.0f;
    const float W = Bounds.Width - 28.0f;

    Surface.Text({ X, Y }, 12.5f, C.TextPrimary, "FLUID SCENE OUTLINER");
    Y += 22.0f;
    Surface.FillRectangle({ X, Y, W, 1.0f }, C.Hairline, 0.0f);
    Y += 10.0f;

    struct NodeEntry { FluidOutlinerNode Id; const char* Label; const char* Tag; };
    constexpr std::array<NodeEntry, 7> Nodes {{
        { FluidOutlinerNode::HeroVolume,     "Hero Pyro Volume",  "GRID" },
        { FluidOutlinerNode::WorldDriftGrid, "World Drift Grid",  "DRIFT" },
        { FluidOutlinerNode::EmitterSource,  "Fuel & Heat Emitter","SRC" },
        { FluidOutlinerNode::SceneObstacle,  "Scene Obstacle",    "SDF" },
        { FluidOutlinerNode::ForceFields,    "ForceFields",       "WIND" },
        { FluidOutlinerNode::ParticleEmbers, "Embers & Shrapnel", "PART" },
        { FluidOutlinerNode::GiLighting,     "GI Fire & Shadows", "GI" },
    }};

    for (const auto& N : Nodes)
    {
        const Rectangle2D RowRect { X, Y, W, 28.0f };
        const bool Selected = (Session.SelectedNode == N.Id);
        const bool Hovered  = RowRect.Contains({ Pointer.X, Pointer.Y });
        if (Hovered && Pointer.Pressed)
        {
            Session.SelectedNode = N.Id;
        }

        const ColorRgba Bg = Selected ? C.WarmAccent.ScaledAlpha(0.22f)
                           : Hovered  ? C.CardElevated
                                      : ColorRgba{ 0u, 0u, 0u, 0u };
        if (Bg.Alpha > 0u)
        {
            Surface.FillRectangle(RowRect, Bg, 6.0f);
        }
        if (Selected)
        {
            Surface.StrokeRectangle(RowRect, C.WarmAccent, 6.0f, 1.0f);
        }

        Surface.Text({ RowRect.X + 10.0f, RowRect.Y + 7.5f }, 11.5f, Selected ? C.TextPrimary : C.TextSoft, N.Label);
        Surface.TextRightAligned({ RowRect.X, RowRect.Y + 8.5f, RowRect.Width - 10.0f, 14.0f }, 9.5f, C.TealTint, N.Tag);
        Y += 32.0f;
    }

    Y += 10.0f;
    Surface.FillRectangle({ X, Y, W, 1.0f }, C.Hairline, 0.0f);
    Y += 12.0f;

    const auto& Hero = Session.InspectSimulation().InspectHeroGrid();
    const auto& Wrld = Session.InspectSimulation().InspectWorldDriftGrid();
    const auto& Doc  = Session.InspectSimulation().InspectDocument();

    char Buf[96] {};
    Surface.Text({ X, Y }, 11.0f, C.TextPrimary, "SIMULATION TELEMETRY");
    Y += 18.0f;

    std::snprintf(Buf, sizeof(Buf), "Hero Grid: %u^3 (Z %.1f..%.1fm)", Hero.Resolution, Hero.BoxMinWorld.Z, Hero.BoxMaxWorld.Z);
    Surface.Text({ X, Y }, 10.0f, C.TextMuted, Buf);
    Y += 15.0f;

    std::snprintf(Buf, sizeof(Buf), "World Grid: %s (%u^3 / %.0fm)",
        Doc.Domain.WorldGridEnabled ? "ON" : "OFF", Wrld.Resolution, Wrld.SpanMeters);
    Surface.Text({ X, Y }, 10.0f, C.TextMuted, Buf);
    Y += 15.0f;

    std::snprintf(Buf, sizeof(Buf), "Step: %llu   Time: %.2fs",
        static_cast<unsigned long long>(Session.InspectSimulation().QueryStepCount()),
        Session.InspectSimulation().QueryElapsedSeconds());
    Surface.Text({ X, Y }, 10.0f, C.TextMuted, Buf);
    Y += 15.0f;

    std::snprintf(Buf, sizeof(Buf), "Emissive GI Lights: %zu",
        Session.InspectSimulation().InspectEmissiveLights().size());
    Surface.Text({ X, Y }, 10.0f, C.TextMuted, Buf);
}

} // namespace Frontier::Workspaces
