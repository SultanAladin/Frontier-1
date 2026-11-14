/*============================================================================================================================================
                                                         PARTICLEVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleViewportHost (Viewport Bar, 3D particle/ribbon/force-ring preview, 5-tool rail, Axis Gizmo, Playback, Ruler & Log Drawer)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Particle/ParticleViewportHost.h"

#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/TransferFunction.h"
#include "Engine/Components/Controls/SimulationPlaybackControls.h"
#include "Engine/Components/Controls/SimulationTimeRuler.h"
#include "Engine/Components/Diagnostics/LogStreamDrawer.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

using namespace ParticleAppearance;

[[nodiscard]] const char* QueryProjectionLabel(ViewportProjectionCategory Mode) noexcept
{
    switch (Mode)
    {
    case ViewportProjectionCategory::PerspectiveOrbit: return "Perspective orbit";
    case ViewportProjectionCategory::TopLattice:       return "Top lattice";
    case ViewportProjectionCategory::FrontAxis:        return "Front axis";
    case ViewportProjectionCategory::SideElevation:    return "Side elevation";
    }
    return "Perspective orbit";
}

[[nodiscard]] const char* QueryOverlayLabel(FieldOverlayChannel Ch) noexcept
{
    switch (Ch)
    {
    case FieldOverlayChannel::CleanViewport: return "Clean viewport (no overlay)";
    case FieldOverlayChannel::VelocityDrift: return "Field · Drift (m/s)";
    case FieldOverlayChannel::Acceleration:  return "Field · Accelerate (m/s^2)";
    case FieldOverlayChannel::Damping:       return "Field · Damp (1/s)";
    }
    return "Clean viewport (no overlay)";
}

[[nodiscard]] ColorRgba SampleRigSrgb(const ColourSpace::ColourSourceProfile& Profile, float T, uint8_t Alpha) noexcept
{
    const ColourSpace::LinearRgba Lin = ColourSpace::SampleColourSource(Profile, { T });
    const ColourSpace::SrgbRgba8  Pix = ColourSpace::ConvertLinearToSrgb8(Lin, ColourSpace::TransferCurveCategory::PureGamma22);
    return { Pix.Red, Pix.Green, Pix.Blue, Alpha };
}

void DrawToolGlyph(PixelSpace& Surface, Point2D C, uint32_t ToolIndex, ColorRgba Ink) noexcept
{
    if (ToolIndex == 0u) // Orbit rings
    {
        Surface.StrokeCircle({ C.X - 2.2f, C.Y }, 4.6f, Ink, 1.25f);
        Surface.StrokeCircle({ C.X + 2.2f, C.Y }, 4.6f, Ink, 1.25f);
    }
    else if (ToolIndex == 1u) // 3D Handles target
    {
        Surface.StrokeCircle(C, 5.2f, Ink, 1.25f);
        Surface.FillCircle(C, 1.8f, Ink);
    }
    else if (ToolIndex == 2u) // Throw ball sphere
    {
        Surface.StrokeCircle(C, 5.2f, Ink, 1.25f);
        Surface.StrokeLine({ C.X - 4.0f, C.Y - 1.5f }, { C.X + 4.0f, C.Y + 2.2f }, Ink, 1.1f);
    }
    else if (ToolIndex == 3u) // Wind probe crosshair
    {
        Surface.StrokeCircle(C, 4.4f, Ink, 1.2f);
        Surface.StrokeLine({ C.X - 6.5f, C.Y }, { C.X + 6.5f, C.Y }, Ink, 1.1f);
        Surface.StrokeLine({ C.X, C.Y - 6.5f }, { C.X, C.Y + 6.5f }, Ink, 1.1f);
    }
    else // Turntable rotation arrow
    {
        Surface.StrokeCircle(C, 4.8f, Ink, 1.2f);
        Surface.FillCircle({ C.X + 4.8f, C.Y }, 1.6f, Ink);
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           LIVE 3D PREVIEW PROJECTION
//------------------------------------------------------------------------------------------------------------------------

void ParticleViewportHost::RenderViewportPreview3D(
    PixelSpace&                     Surface,
    Rectangle2D                     ViewportRect,
    const ParticleWorkspaceSession& Session) const noexcept
{
    const auto C = QueryStudioColors();
    Surface.PushClip(ViewportRect);
    Surface.FillRectangle(ViewportRect, C.ViewportBg, 14.0f);

    const Point2D Center = ViewportRect.Center();
    const float   Focal  = std::min(ViewportRect.Width, ViewportRect.Height) * 0.68f;
    const float   CosY   = std::cos(Session.OrbitYawRad);
    const float   SinY   = std::sin(Session.OrbitYawRad);
    const float   CosP   = std::cos(Session.OrbitPitchRad);
    const float   SinP   = std::sin(Session.OrbitPitchRad);
    const float   Dist   = std::max(3.5f, Session.OrbitDistanceM);

    auto Project3D = [&](float Wx, float Wy, float Wz, Point2D& OutPt, float& OutDepth) noexcept -> bool {
        const float Rx = Wx * CosY - Wz * SinY;
        const float Rz = Wx * SinY + Wz * CosY;
        const float Ry = Wy * CosP - Rz * SinP;
        const float Fz = Wy * SinP + Rz * CosP + Dist;
        if (Fz <= 0.35f) return false;
        OutPt.X  = Center.X + (Rx / Fz) * Focal;
        OutPt.Y  = Center.Y - (Ry / Fz) * Focal + 24.0f;
        OutDepth = Fz;
        return true;
    };

    // 1. Subtle 3D perspective floor lattice grid (12 x 12 m)
    const ColorRgba GridCol { 255u, 255u, 255u, 15u };
    for (int I = -6; I <= 6; ++I)
    {
        Point2D A0 {}, A1 {}, B0 {}, B1 {};
        float D0 = 0.0f, D1 = 0.0f;
        const float Coord = static_cast<float>(I);
        if (Project3D(Coord, -2.0f, -6.0f, A0, D0) && Project3D(Coord, -2.0f, 6.0f, A1, D1))
        {
            Surface.StrokeLine(A0, A1, (I == 0) ? ColorRgba { 255u, 255u, 255u, 28u } : GridCol, 0.9f);
        }
        if (Project3D(-6.0f, -2.0f, Coord, B0, D0) && Project3D(6.0f, -2.0f, Coord, B1, D1))
        {
            Surface.StrokeLine(B0, B1, (I == 0) ? ColorRgba { 255u, 255u, 255u, 28u } : GridCol, 0.9f);
        }
    }

    // 2. Subtle central obsidian sphere & translucent force-field reach rings
    Point2D CorePt {};
    float   CoreDepth = 10.0f;
    if (Project3D(0.0f, 0.45f, 0.0f, CorePt, CoreDepth))
    {
        const float SphereR = (1.65f / CoreDepth) * Focal;
        Surface.FillCircle(CorePt, SphereR, ColorRgba { 16u, 20u, 26u, 225u });
        Surface.StrokeCircle(CorePt, SphereR, ColorRgba { 130u, 178u, 210u, 48u }, 1.2f);
        Surface.FillCircle({ CorePt.X - SphereR * 0.25f, CorePt.Y - SphereR * 0.25f }, SphereR * 0.48f, ColorRgba { 210u, 236u, 255u, 24u });
    }

    // Force field source wireframe rings when Handles or Field Overlay are enabled
    if (Session.ShowHandles3D || Session.ShowFieldOverlay)
    {
        for (const auto& Force : Session.ForceSources)
        {
            if (!Force.Visible) continue;
            Point2D Fp {};
            float   Fd = 10.0f;
            if (Project3D(Force.CentreMeters.X * 0.45f, Force.CentreMeters.Y * 0.45f - 0.5f, Force.CentreMeters.Z * 0.45f, Fp, Fd))
            {
                const float R = std::clamp((Force.RadiusMeters * 0.30f / Fd) * Focal, 14.0f, 64.0f);
                const ColorRgba RingTint = (Force.Model == SimulationSpace::ForceFields::ForceModel::Attractor || Force.Model == SimulationSpace::ForceFields::ForceModel::Repulsor)
                    ? ColorRgba { 150u, 122u, 235u, 72u }
                    : ColorRgba { 88u,  196u, 210u, 62u };
                Surface.StrokeCircle(Fp, R, RingTint, 1.0f);
                Surface.FillCircle(Fp, 3.0f, C.WarmAccent.ScaledAlpha(0.85f));
            }
        }
    }

    // 3. Vivid 3D pouring bokeh ribbons & glowing particles driven by the active rig's ColourSpace::ColourSourceProfile!
    const AuthoredParticleRigUi* SelectedRig = Session.QuerySelectedRig();
    const ColourSpace::ColourSourceProfile& ActiveRamp =
        (SelectedRig != nullptr) ? SelectedRig->ColourProfile
                                 : (!Session.ParticleRigs.empty() ? Session.ParticleRigs[0].ColourProfile : ColourSpace::ColourSourceProfile {});
    const uint32_t ApertureStyle = (SelectedRig != nullptr) ? SelectedRig->BokehApertureStyle : 0u;
    const float    PhaseSec      = static_cast<float>(Session.ElapsedSeconds);

    constexpr int RibbonCount = 46;
    constexpr int SegCount    = 14;
    for (int R = 0; R < RibbonCount; ++R)
    {
        const float SeedF  = static_cast<float>(R);
        const float Az0    = SeedF * 2.39996f + PhaseSec * 0.28f;
        const float Elev0  = std::sin(SeedF * 1.71f) * 0.92f;
        const float Swirl  = (R % 2 == 0 ? 1.0f : -1.0f) * (0.85f + 0.35f * std::sin(SeedF * 0.9f));
        const float Speed  = 0.55f + 0.45f * std::fmod(SeedF * 0.37f, 1.0f);

        std::array<Point2D, SegCount> Trail {};
        int ValidCount = 0;
        Point2D HeadPt {};
        float   HeadDepth = 10.0f;

        for (int S = 0; S < SegCount; ++S)
        {
            const float U     = static_cast<float>(S) / static_cast<float>(SegCount - 1);
            const float Age   = std::fmod(U * 1.45f + PhaseSec * Speed + SeedF * 0.19f, 2.2f);
            const float Rad   = 1.62f + Age * 1.38f + 0.32f * std::sin(Age * 2.6f + SeedF);
            const float Theta = Az0 + Age * Swirl * 1.15f;
            const float Phi   = Elev0 + 0.48f * std::sin(Age * 1.8f + SeedF * 0.6f);

            const float Wx = Rad * std::cos(Phi) * std::cos(Theta);
            const float Wy = 0.45f + Rad * std::sin(Phi) * 0.88f - Age * Age * 0.14f;
            const float Wz = Rad * std::cos(Phi) * std::sin(Theta);

            Point2D Pt {};
            float   Dp = 10.0f;
            if (Project3D(Wx, Wy, Wz, Pt, Dp))
            {
                Trail[static_cast<std::size_t>(ValidCount++)] = Pt;
                HeadPt    = Pt;
                HeadDepth = Dp;
            }
        }

        const float RampT = std::clamp(static_cast<float>(R) / static_cast<float>(RibbonCount - 1), 0.0f, 1.0f);
        if (ValidCount >= 2)
        {
            const ColorRgba RibbonTint = SampleRigSrgb(ActiveRamp, RampT, 128u);
            Surface.StrokePolyline(std::span<const Point2D>(Trail.data(), static_cast<std::size_t>(ValidCount)), RibbonTint, 1.15f, false);
        }

        // Bokeh head matching selected Optical Bokeh Aperture style (0=Mixed, 1=Sharp, 2=Creamy, 3=Rim Ring)
        const float DepthScale = std::clamp(11.0f / std::max(3.0f, HeadDepth), 0.55f, 1.85f);
        const float HeadR      = (2.4f + 2.8f * std::fmod(SeedF * 0.618f, 1.0f)) * DepthScale;
        const ColorRgba HeadCol = SampleRigSrgb(ActiveRamp, std::clamp(RampT * 0.7f + 0.25f, 0.0f, 1.0f), 220u);

        if (ApertureStyle == 3u) // Rim Ring
        {
            Surface.StrokeCircle(HeadPt, HeadR * 1.25f, HeadCol, 1.35f);
        }
        else if (ApertureStyle == 1u) // Sharp pinpoint
        {
            Surface.FillCircle(HeadPt, HeadR * 0.55f, { 255u, 255u, 255u, 240u });
            Surface.FillCircle(HeadPt, HeadR * 1.1f, HeadCol.ScaledAlpha(0.28f));
        }
        else if (ApertureStyle == 2u) // Creamy soft disc
        {
            Surface.FillCircle(HeadPt, HeadR * 1.55f, HeadCol.ScaledAlpha(0.22f));
            Surface.FillCircle(HeadPt, HeadR * 0.95f, HeadCol.ScaledAlpha(0.55f));
        }
        else // Mixed depth bokeh
        {
            if (R % 3 == 0)
            {
                Surface.FillCircle(HeadPt, HeadR * 1.45f, HeadCol.ScaledAlpha(0.24f));
                Surface.FillCircle(HeadPt, HeadR * 0.85f, HeadCol.ScaledAlpha(0.68f));
            }
            else
            {
                Surface.FillCircle(HeadPt, HeadR * 0.62f, { 248u, 252u, 255u, 235u });
            }
        }
    }

    // 3b. Project live simulated C++ ParticleExtension slots directly into the 3D viewport stage
    const auto LiveSlots = Session.SimulationExtension.InspectParticleSlots();
    uint32_t DrawnLive = 0u;
    for (std::size_t S = 0u; S < LiveSlots.size() && DrawnLive < 480u; ++S)
    {
        const auto& Slot = LiveSlots[S];
        if (!ParticleSpace::IsParticleSlotAlive(Slot)) continue;
        Point2D Pt {};
        float   Dp = 10.0f;
        if (!Project3D(Slot.PositionMeters[0], Slot.PositionMeters[1], Slot.PositionMeters[2], Pt, Dp)) continue;

        const ColourSpace::LinearRgba Lin { Slot.ColourLinear[0], Slot.ColourLinear[1], Slot.ColourLinear[2], 1.0f };
        const ColourSpace::SrgbRgba8  Pix = ColourSpace::ConvertLinearToSrgb8(Lin, ColourSpace::TransferCurveCategory::PureGamma22);
        const float DepthScale = std::clamp(11.0f / std::max(3.0f, Dp), 0.55f, 1.75f);
        const float Rad        = std::clamp(Slot.SizeMeters * 38.0f * DepthScale, 1.4f, 4.8f);
        Surface.FillCircle(Pt, Rad * 1.45f, ColorRgba { Pix.Red, Pix.Green, Pix.Blue, 52u });
        Surface.FillCircle(Pt, Rad * 0.65f, ColorRgba { Pix.Red, Pix.Green, Pix.Blue, 210u });
        ++DrawnLive;
    }

    // 4. Optional Lattice Domain Minimap inset (12 x 12 m)
    if (Session.ShowMinimap)
    {
        const Rectangle2D Mini { ViewportRect.Right() - 148.0f, ViewportRect.Y + 46.0f, 134.0f, 118.0f };
        Surface.FillRectangle(Mini, ColorRgba { 14u, 16u, 20u, 232u }, 12.0f);
        Surface.StrokeRectangle(Mini, C.HairlineStrong, 12.0f, 1.0f);
        const Rectangle2D MapPlot { Mini.X + 10.0f, Mini.Y + 10.0f, Mini.Width - 20.0f, Mini.Height - 32.0f };
        Surface.FillRectangle(MapPlot, ColorRgba { 8u, 10u, 14u, 255u }, 6.0f);
        for (const auto& F : Session.ForceSources)
        {
            const float Fx = MapPlot.Center().X + std::clamp(F.CentreMeters.X / 6.0f, -0.9f, 0.9f) * (MapPlot.Width * 0.45f);
            const float Fy = MapPlot.Center().Y + std::clamp(F.CentreMeters.Z / 6.0f, -0.9f, 0.9f) * (MapPlot.Height * 0.45f);
            Surface.StrokeCircle({ Fx, Fy }, 8.0f, C.TealTint.ScaledAlpha(0.55f), 1.0f);
            Surface.FillCircle({ Fx, Fy }, 2.4f, C.WarmAccent);
        }
        Surface.Text({ Mini.X + 10.0f, Mini.Bottom() - 16.0f }, 8.2f, C.TextMuted, "Lattice domain");
        Surface.TextRightAligned({ Mini.X, Mini.Bottom() - 16.0f, Mini.Width - 10.0f, 12.0f }, 8.2f, C.TextSoft, "12 x 12 m");
    }

    Surface.PopClip();
}

//------------------------------------------------------------------------------------------------------------------------
//                                           PARTICLEVIEWPORTHOST::RENDER
//------------------------------------------------------------------------------------------------------------------------

void ParticleViewportHost::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    ParticleWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     Opacity) noexcept
{
    if (Bounds.Width < 260.0f || Bounds.Height < 260.0f || Opacity <= 0.001f)
    {
        return;
    }

    const auto C = QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg, 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 22.0f, 1.0f);

    // 1. Top 46px Viewport Bar
    const Rectangle2D TopBar { Bounds.X + 10.0f, Bounds.Y + 6.0f, Bounds.Width - 20.0f, 38.0f };
    float BarX = TopBar.X + 4.0f;
    const float BarCY = TopBar.Center().Y;

    DrawSparkleGlyph(Surface, { BarX + 7.0f, BarCY }, 5.5f, C.TextSoft);
    Surface.Text({ BarX + 18.0f, BarCY - 10.0f }, 11.2f, C.TextPrimary, "Scene");
    Surface.Text({ BarX + 18.0f, BarCY + 2.0f },  10.8f, C.TextPrimary, "view");
    BarX += 66.0f;

    // Projection dropdown pill
    const Rectangle2D ProjPill { BarX, BarCY - 13.0f, 134.0f, 26.0f };
    if (ConstructDropdownPill(Surface, ProjPill, QueryProjectionLabel(Session.Projection), Pointer, true))
    {
        const uint8_t Next = static_cast<uint8_t>((static_cast<uint8_t>(Session.Projection) + 1u) % 4u);
        Session.Projection = static_cast<ViewportProjectionCategory>(Next);
    }
    BarX = ProjPill.Right() + 8.0f;

    // Overlay channel dropdown pill
    const float OverlayW = std::min(190.0f, std::max(120.0f, TopBar.Right() - BarX - 246.0f));
    const Rectangle2D OverlayPill { BarX, BarCY - 13.0f, OverlayW, 26.0f };
    if (ConstructDropdownPill(Surface, OverlayPill, QueryOverlayLabel(Session.OverlayChannel), Pointer, true))
    {
        const uint8_t Next = static_cast<uint8_t>((static_cast<uint8_t>(Session.OverlayChannel) + 1u) % 4u);
        Session.OverlayChannel = static_cast<FieldOverlayChannel>(Next);
        Session.ShowFieldOverlay = (Session.OverlayChannel != FieldOverlayChannel::CleanViewport);
    }

    // Right-side controls in Viewport Bar: Throw Ball · T, Field · V, and 4 compact icon buttons
    float RightX = TopBar.Right() - 4.0f;
    auto DrawBarIcon = [&](bool Active, auto DrawFn) -> bool {
        RightX -= 24.0f;
        const Rectangle2D R { RightX, BarCY - 11.0f, 22.0f, 22.0f };
        const bool Hov = ControlAppearance::QueryPointerOver(R, Pointer);
        if (Active || Hov)
        {
            Surface.FillRectangle(R, Active ? C.CardElevated : ColorRgba { 30u, 30u, 32u, 255u }, 6.0f);
            if (Active) Surface.StrokeRectangle(R, C.HairlineStrong, 6.0f, 0.8f);
        }
        DrawFn(R.Center(), Active ? C.TextPrimary : C.TextMuted);
        RightX -= 4.0f;
        return Hov && Pointer.Pressed;
    };

    // Maximize icon
    if (DrawBarIcon(Session.ViewportMaximized, [&](Point2D Pt, ColorRgba Ink) {
        Surface.StrokeRectangle({ Pt.X - 4.5f, Pt.Y - 4.5f, 9.0f, 9.0f }, Ink, 1.5f, 1.1f);
    })) { Session.ViewportMaximized = !Session.ViewportMaximized; }

    // Log Drawer icon
    if (DrawBarIcon(Session.ShowLogDrawer, [&](Point2D Pt, ColorRgba Ink) {
        Surface.StrokeRectangle({ Pt.X - 5.0f, Pt.Y - 4.5f, 10.0f, 9.0f }, Ink, 2.0f, 1.1f);
        Surface.StrokeLine({ Pt.X - 2.0f, Pt.Y - 1.5f }, { Pt.X + 2.5f, Pt.Y - 1.5f }, Ink, 1.1f);
        Surface.StrokeLine({ Pt.X - 2.0f, Pt.Y + 1.5f }, { Pt.X + 1.0f, Pt.Y + 1.5f }, Ink, 1.1f);
    })) { Session.ShowLogDrawer = !Session.ShowLogDrawer; }

    // Minimap icon
    if (DrawBarIcon(Session.ShowMinimap, [&](Point2D Pt, ColorRgba Ink) {
        Surface.StrokeRectangle({ Pt.X - 5.0f, Pt.Y - 4.5f, 10.0f, 9.0f }, Ink, 1.5f, 1.1f);
        Surface.StrokeLine({ Pt.X - 1.5f, Pt.Y - 4.5f }, { Pt.X - 1.5f, Pt.Y + 4.5f }, Ink, 1.0f);
        Surface.StrokeLine({ Pt.X + 1.8f, Pt.Y - 4.5f }, { Pt.X + 1.8f, Pt.Y + 4.5f }, Ink, 1.0f);
    })) { Session.ShowMinimap = !Session.ShowMinimap; }

    // Vector arrows icon
    if (DrawBarIcon(Session.ShowFieldArrows, [&](Point2D Pt, ColorRgba Ink) {
        DrawWindLinesGlyph(Surface, Pt, 4.6f, Ink);
    })) { Session.ShowFieldArrows = !Session.ShowFieldArrows; }

    // Field · V pill
    RightX -= 58.0f;
    const Rectangle2D FieldPill { RightX, BarCY - 13.0f, 54.0f, 26.0f };
    const bool FieldHov = ControlAppearance::QueryPointerOver(FieldPill, Pointer);
    Surface.FillRectangle(FieldPill, Session.ShowFieldOverlay ? C.WarmSoft : C.CardBg, 13.0f);
    Surface.StrokeRectangle(FieldPill, Session.ShowFieldOverlay ? C.WarmAccent : (FieldHov ? C.HairlineStrong : C.Hairline), 13.0f, 1.0f);
    Surface.TextCentered(FieldPill, 9.6f, Session.ShowFieldOverlay ? C.WarmAccent : C.TextSoft, "Field · V");
    if (FieldHov && Pointer.Pressed)
    {
        Session.ShowFieldOverlay = !Session.ShowFieldOverlay;
    }

    // Throw Ball · T pill
    RightX -= 88.0f;
    const Rectangle2D BallPill { RightX, BarCY - 13.0f, 82.0f, 26.0f };
    const bool BallActive = (Session.ActiveTool == ViewportToolCategory::ThrowBall);
    const bool BallHov    = ControlAppearance::QueryPointerOver(BallPill, Pointer);
    Surface.FillRectangle(BallPill, BallActive ? C.WarmSoft : C.CardBg, 13.0f);
    Surface.StrokeRectangle(BallPill, BallActive ? C.WarmAccent : (BallHov ? C.HairlineStrong : C.Hairline), 13.0f, 1.0f);
    Surface.TextCentered(BallPill, 9.6f, BallActive ? C.WarmAccent : C.TextSoft, "Throw Ball · T");
    if (BallHov && Pointer.Pressed)
    {
        Session.ActiveTool = BallActive ? ViewportToolCategory::OrbitCamera : ViewportToolCategory::ThrowBall;
        Session.AppendLog(LogChannelCategory::Viewport, BallActive ? "Returned to Orbit camera" : "Throw Ball tool active (T)");
    }

    // 2. Vertical partitioning: Viewport Surface + Playback (52px) + TimeRuler (44px) + optional LogDrawer (112px)
    const float PlaybackH = SimulationPlaybackControls::RecommendedHeightPx;
    const float RulerH    = SimulationTimeRuler::RecommendedHeightPx;
    const float LogH      = Session.ShowLogDrawer ? LogStreamDrawer::RecommendedHeightPx : 0.0f;
    const float SurfaceY  = TopBar.Bottom() + 6.0f;
    const float SurfaceH  = std::max(160.0f, Bounds.Bottom() - SurfaceY - PlaybackH - RulerH - LogH - 6.0f);

    const Rectangle2D ViewportRect { Bounds.X + 8.0f, SurfaceY, Bounds.Width - 16.0f, SurfaceH };
    LastSurfaceBounds = ViewportRect;

    // Render live 3D particle/field canvas
    RenderViewportPreview3D(Surface, ViewportRect, Session);
    Surface.StrokeRectangle(ViewportRect, C.Hairline, 14.0f, 1.0f);

    // Orbit drag on empty viewport surface
    const bool OverViewport = ControlAppearance::QueryPointerOver(ViewportRect, Pointer);
    if (OverViewport && Pointer.Pressed && Pointer.X > ViewportRect.X + 54.0f)
    {
        DraggingOrbit  = true;
        LastPointerPos = { Pointer.X, Pointer.Y };
    }
    if (!Pointer.Down)
    {
        DraggingOrbit = false;
    }
    if (DraggingOrbit)
    {
        const float Dx = Pointer.X - LastPointerPos.X;
        const float Dy = Pointer.Y - LastPointerPos.Y;
        Session.OrbitYawRad   += Dx * 0.008f;
        Session.OrbitPitchRad  = std::clamp(Session.OrbitPitchRad + Dy * 0.008f, -1.1f, 1.25f);
        LastPointerPos         = { Pointer.X, Pointer.Y };
    }

    // Top-left caption pills (● LIVE + Selected Rig pill)
    const float HudY = ViewportRect.Y + 12.0f;
    const Rectangle2D LivePill { ViewportRect.X + 12.0f, HudY, 64.0f, 22.0f };
    Surface.FillRectangle(LivePill, ColorRgba { 16u, 18u, 20u, 225u }, 11.0f);
    Surface.StrokeRectangle(LivePill, C.HairlineStrong, 11.0f, 0.9f);
    Surface.FillCircle({ LivePill.X + 12.0f, LivePill.Center().Y }, 3.4f, Session.Playing ? C.LiveGreen : C.WarmAccent);
    Surface.Text({ LivePill.X + 22.0f, LivePill.Center().Y - 5.2f }, 10.0f, C.TextPrimary, Session.Playing ? "LIVE" : "HOLD");

    const char* FocusName = "Force Field";
    if (const auto* Rig = Session.QuerySelectedRig())           FocusName = Rig->DisplayName.c_str();
    else if (const auto* Force = Session.QuerySelectedForce())  FocusName = Force->Label.c_str();
    else if (Session.SelectionKind == ParticleSelectionCategory::AmbientWind) FocusName = "Ambient Wind";

    const float NamePillW = std::min(210.0f, Surface.MeasureTextWidth(10.2f, FocusName) + 22.0f);
    const Rectangle2D NamePill { LivePill.Right() + 8.0f, HudY, NamePillW, 22.0f };
    Surface.FillRectangle(NamePill, ColorRgba { 18u, 19u, 22u, 220u }, 11.0f);
    Surface.StrokeRectangle(NamePill, C.Hairline, 11.0f, 0.9f);
    Surface.TextCentered(NamePill, 10.0f, C.TextPrimary, FocusName);

    // Top-right HUD telemetry pills (PARTICLES / RATE / GPU)
    const uint32_t AliveTotal = Session.CountTotalAliveParticles();
    const uint32_t CapTotal   = Session.CountTotalParticleCapacity();
    char ParticlesHud[48], RateHud[28], GpuHud[28];
    if (AliveTotal >= 1000u && CapTotal >= 1000u)
    {
        std::snprintf(ParticlesHud, sizeof(ParticlesHud), "PARTICLES %u,%03u / %u,%03u",
            AliveTotal / 1000u, AliveTotal % 1000u, CapTotal / 1000u, CapTotal % 1000u);
    }
    else
    {
        std::snprintf(ParticlesHud, sizeof(ParticlesHud), "PARTICLES %u / %u", AliveTotal, CapTotal);
    }
    std::snprintf(RateHud, sizeof(RateHud), "RATE %.0f fps", Session.MeasuredFps);
    std::snprintf(GpuHud,  sizeof(GpuHud),  "GPU %.2f ms", Session.ComputeTimeMs);

    float HudRX = ViewportRect.Right() - 12.0f;
    auto DrawHudPill = [&](const char* Text) {
        const float W = Surface.MeasureTextWidth(9.8f, Text) + 26.0f;
        HudRX -= W;
        const Rectangle2D P { HudRX, HudY, W, 22.0f };
        Surface.FillRectangle(P, ColorRgba { 16u, 17u, 20u, 225u }, 11.0f);
        Surface.StrokeRectangle(P, C.Hairline, 11.0f, 0.9f);
        Surface.TextCentered(P, 9.8f, C.TextSoft, Text);
        HudRX -= 6.0f;
    };
    DrawHudPill(GpuHud);
    DrawHudPill(RateHud);
    DrawHudPill(ParticlesHud);

    // Left floating vertical 5-tool pill rail
    const Rectangle2D RailRect { ViewportRect.X + 12.0f, ViewportRect.Y + 62.0f, 34.0f, 168.0f };
    Surface.FillRectangle(RailRect, ColorRgba { 16u, 17u, 20u, 228u }, 17.0f);
    Surface.StrokeRectangle(RailRect, C.Hairline, 17.0f, 0.9f);
    for (uint32_t T = 0u; T < 5u; ++T)
    {
        const Rectangle2D Btn { RailRect.X + 4.0f, RailRect.Y + 6.0f + static_cast<float>(T) * 32.0f, 26.0f, 26.0f };
        const bool Active = (static_cast<uint32_t>(Session.ActiveTool) == T);
        const bool Hov    = ControlAppearance::QueryPointerOver(Btn, Pointer);
        if (Active || Hov)
        {
            Surface.FillCircle(Btn.Center(), 12.5f, Active ? ColorRgba { 48u, 50u, 54u, 255u } : ColorRgba { 32u, 33u, 36u, 255u });
            if (Active) Surface.StrokeCircle(Btn.Center(), 12.5f, C.HairlineStrong, 1.0f);
        }
        DrawToolGlyph(Surface, Btn.Center(), T, Active ? C.TextPrimary : C.TextMuted);
        if (Hov && Pointer.Pressed)
        {
            Session.ActiveTool = static_cast<ViewportToolCategory>(T);
            if (T == 1u) Session.ShowHandles3D = !Session.ShowHandles3D;
        }
    }

    // Bottom-left RGB X/Y/Z World Axis Gizmo
    const Point2D GizmoO { ViewportRect.X + 36.0f, ViewportRect.Bottom() - 40.0f };
    Surface.StrokeLine(GizmoO, { GizmoO.X, GizmoO.Y - 22.0f }, { 92u,  196u, 128u, 235u }, 1.5f);
    Surface.StrokeLine(GizmoO, { GizmoO.X - 18.0f, GizmoO.Y + 12.0f }, { 226u, 108u, 108u, 235u }, 1.5f);
    Surface.StrokeLine(GizmoO, { GizmoO.X + 18.0f, GizmoO.Y + 12.0f }, { 104u, 166u, 238u, 235u }, 1.5f);
    Surface.FillCircle(GizmoO, 2.5f, C.TextSoft);
    Surface.Text({ GizmoO.X - 3.0f, GizmoO.Y - 34.0f }, 8.6f, { 112u, 214u, 146u, 255u }, "Y");
    Surface.Text({ GizmoO.X - 28.0f, GizmoO.Y + 12.0f }, 8.6f, { 236u, 126u, 126u, 255u }, "X");
    Surface.Text({ GizmoO.X + 20.0f, GizmoO.Y + 12.0f }, 8.6f, { 122u, 180u, 246u, 255u }, "Z");

    // Bottom-center floating shortcut hint pill
    const char* HelpHint = "Drag handles (Shift: Y)  ·  G handles  ·  Drop OBJ/STL  ·  T throw ball  ·  Space pause  ·  V field";
    const float HintW = std::min(ViewportRect.Width - 110.0f, Surface.MeasureTextWidth(9.8f, HelpHint) + 32.0f);
    const Rectangle2D HintPill { ViewportRect.Center().X - HintW * 0.5f, ViewportRect.Bottom() - 32.0f, HintW, 22.0f };
    Surface.FillRectangle(HintPill, ColorRgba { 16u, 17u, 20u, 220u }, 11.0f);
    Surface.StrokeRectangle(HintPill, C.Hairline, 11.0f, 0.9f);
    Surface.TextCentered(HintPill, 9.6f, C.TextMuted, HelpHint);

    // 3. Reusable SimulationPlaybackControls (52px)
    const Rectangle2D PlaybackRect { Bounds.X + 4.0f, ViewportRect.Bottom() + 4.0f, Bounds.Width - 8.0f, PlaybackH };
    LastPlaybackBounds = PlaybackRect;
    SimulationPlaybackReading PlayReading {};
    PlayReading.Playing          = Session.Playing;
    PlayReading.ElapsedSeconds   = Session.ElapsedSeconds;
    PlayReading.TimeScale        = Session.TimeScale;
    PlayReading.LiveUncached     = true;
    PlayReading.ShowBurstTrigger = true;

    const SimulationPlaybackResult PlayResult = SimulationPlaybackControls::Construct(Surface, PlaybackRect, PlayReading, Pointer, Opacity);
    if (PlayResult.TogglePlayPressed) Session.Playing = !Session.Playing;
    if (PlayResult.ResetPressed)      Session.ResetPlayback();
    if (PlayResult.SingleStepPressed) { Session.Playing = false; Session.ElapsedSeconds += 1.0 / 60.0; ++Session.StepCounter; }
    if (PlayResult.TimeScaleChanged)  Session.TimeScale = PlayResult.NewTimeScale;
    if (PlayResult.BurstPressed)      Session.TriggerBurst();

    // 4. Reusable SimulationTimeRuler (44px)
    const Rectangle2D RulerRect { Bounds.X + 4.0f, PlaybackRect.Bottom(), Bounds.Width - 8.0f, RulerH };
    LastRulerBounds = RulerRect;
    SimulationTimeRulerReading RulerReading {};
    RulerReading.ElapsedSeconds     = Session.ElapsedSeconds;
    RulerReading.LoopDurationSec    = Session.LoopDurationSec;
    RulerReading.StepCounter        = Session.StepCounter;
    RulerReading.ActiveTrackEndFrac = 0.38f;

    const SimulationTimeRulerResult RulerResult = SimulationTimeRuler::Construct(Surface, RulerRect, RulerReading, Session.RulerSession, Pointer, Opacity);
    if (RulerResult.Scrubbed)
    {
        Session.ElapsedSeconds = RulerResult.ScrubbedTimeSec;
        Session.StepCounter    = static_cast<uint32_t>(std::round(Session.ElapsedSeconds * 60.0));
    }

    // 5. Optional Collapsible LogStreamDrawer
    if (Session.ShowLogDrawer)
    {
        const Rectangle2D LogRect { Bounds.X + 4.0f, RulerRect.Bottom(), Bounds.Width - 8.0f, LogH };
        LogStreamDrawer::Construct(Surface, LogRect, Session.LogRecords, Session.LogDrawerSession, Pointer, Opacity);
    }
}

} // namespace Frontier::Workspaces
