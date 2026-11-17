/*============================================================================================================================================
                                                           FLUIDVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidViewportHost: HTML-matched Scene View bar, 3D floor grid, wireframe bounds, volumetric fire, Diagnostics & Ruler

#include "Engine/Workspaces/Fluid/FluidViewportHost.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces
{

namespace
{

using namespace ParticleAppearance;

[[nodiscard]] const char* QueryCameraLabel(FluidCameraPreset Preset) noexcept
{
    switch (Preset)
    {
    case FluidCameraPreset::Perspective: return "Perspective";
    case FluidCameraPreset::Front:       return "Front";
    case FluidCameraPreset::Side:        return "Side";
    case FluidCameraPreset::Top:         return "Top";
    }
    return "Perspective";
}

[[nodiscard]] const char* QueryChannelLabel(SimulationSpace::FluidSpace::FluidRenderChannel Ch) noexcept
{
    using C = SimulationSpace::FluidSpace::FluidRenderChannel;
    switch (Ch)
    {
    case C::LitVolume:          return "Lit volume";
    case C::VoxelDda:           return "Voxel DDA";
    case C::SmokeDensity:       return "Smoke density";
    case C::Temperature:        return "Temperature";
    case C::Fuel:               return "Fuel";
    case C::Velocity:           return "Velocity";
    case C::Vorticity:          return "Vorticity";
    case C::PressureIrradiance: return "Pressure";
    case C::CrossSection:       return "Cross-section";
    }
    return "Lit volume";
}

[[nodiscard]] const char* QueryBurstLabel(uint32_t Pattern) noexcept
{
    static constexpr std::array<const char*, 5> Labels {{
        "Single burst",
        "Shrapnel",
        "Cluster salvo",
        "Colliding waves",
        "Mushroom cloud",
    }};
    return Labels[Pattern % Labels.size()];
}

void DrawToolIcon(PixelSpace& Surface, Point2D C, uint32_t Index, ColorRgba Ink) noexcept
{
    if (Index == 0u) // Orbit
    {
        Surface.StrokeCircle({ C.X - 2.0f, C.Y }, 4.2f, Ink, 1.2f);
        Surface.StrokeCircle({ C.X + 2.0f, C.Y }, 4.2f, Ink, 1.2f);
    }
    else if (Index == 1u) // Brush
    {
        Surface.StrokeLine({ C.X - 4.5f, C.Y + 4.5f }, { C.X + 4.5f, C.Y - 4.5f }, Ink, 1.4f);
        Surface.FillCircle({ C.X + 4.5f, C.Y - 4.5f }, 1.8f, Ink);
    }
    else if (Index == 2u) // Burst
    {
        Surface.StrokeCircle(C, 4.4f, Ink, 1.2f);
        Surface.FillCircle(C, 1.8f, Ink);
    }
    else if (Index == 3u) // Cross-section
    {
        Surface.StrokeRectangle({ C.X - 4.5f, C.Y - 4.5f, 9.0f, 9.0f }, Ink, 1.5f, 1.1f);
        Surface.StrokeLine({ C.X - 5.5f, C.Y }, { C.X + 5.5f, C.Y }, Ink, 1.1f);
    }
    else // Turntable
    {
        Surface.StrokeCircle(C, 4.6f, Ink, 1.2f);
        Surface.FillCircle({ C.X + 4.6f, C.Y }, 1.6f, Ink);
    }
}

} // namespace

FluidViewportHost::~FluidViewportHost() noexcept
{
    PreviewTexture.DestroyPixels();
}

void FluidViewportHost::UploadPreviewTexture(
    const std::vector<uint8_t>& RgbaPixels,
    uint32_t                    WidthPx,
    uint32_t                    HeightPx)
{
    if (WidthPx == 0u || HeightPx == 0u || RgbaPixels.size() < static_cast<size_t>(WidthPx) * HeightPx * 4u)
    {
        return;
    }

    const bool NeedsCreate =
        PreviewTexture.Width != static_cast<int>(WidthPx) ||
        PreviewTexture.Height != static_cast<int>(HeightPx) ||
        PreviewTexture.Pixels == nullptr;

    if (NeedsCreate)
    {
        PreviewTexture.DestroyPixels();
        PreviewTexture.SetStatus(ImTextureStatus_Destroyed);
        PreviewTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(WidthPx), static_cast<int>(HeightPx));
    }

    if (PreviewTexture.Pixels != nullptr)
    {
        std::memcpy(PreviewTexture.GetPixels(), RgbaPixels.data(), static_cast<size_t>(WidthPx) * HeightPx * 4u);
        PreviewTexture.SetStatus(NeedsCreate ? ImTextureStatus_WantCreate : ImTextureStatus_WantUpdates);
    }
}

void FluidViewportHost::Render(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer,
    float                  Opacity) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg.ScaledAlpha(Opacity), 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline.ScaledAlpha(Opacity), 22.0f, 1.0f);

    // Layout: Top Viewport Bar (40px) + Canvas + Transport Bar (42px) + Ruler (44px)
    const Rectangle2D TopBar    { Bounds.X, Bounds.Y, Bounds.Width, 40.0f };
    const Rectangle2D RulerBar  { Bounds.X, Bounds.Bottom() - 46.0f, Bounds.Width, 46.0f };
    const Rectangle2D TransBar  { Bounds.X, RulerBar.Y - 42.0f, Bounds.Width, 42.0f };
    LastSurfaceBounds = {
        Bounds.X + 2.0f,
        TopBar.Bottom(),
        Bounds.Width - 4.0f,
        std::max(140.0f, TransBar.Y - TopBar.Bottom()),
    };

    auto& Doc = Session.AccessSimulation().AccessDocument();

    //--------------------------------------------------------------------------------------------------------------------
    // 1. Top Viewport Bar ("Scene view | Perspective v ... Bake | Diag | Lit volume v | Bounds | Focus")
    //--------------------------------------------------------------------------------------------------------------------
    Surface.FillRectangle({ TopBar.X + 12.0f, TopBar.Bottom() - 1.0f, TopBar.Width - 24.0f, 1.0f }, C.Hairline, 0.0f);
    float TX = TopBar.X + 16.0f;
    const float TopCY = TopBar.Center().Y;

    Surface.StrokeRectangle({ TX, TopCY - 5.5f, 12.0f, 9.0f }, C.TextMuted, 2.0f, 1.1f);
    Surface.Text({ TX + 18.0f, TopCY - 6.0f }, 11.2f, C.TextPrimary, "Scene view");
    TX += 94.0f;
    Surface.FillRectangle({ TX, TopCY - 7.0f, 1.0f, 14.0f }, C.Hairline, 0.0f);
    TX += 12.0f;

    const Rectangle2D CamPill { TX, TopCY - 12.0f, 96.0f, 24.0f };
    const bool CamHov = ControlAppearance::QueryPointerOver(CamPill, Pointer);
    if (CamHov) Surface.FillRectangle(CamPill, C.CardElevated, 12.0f);
    Surface.Text({ CamPill.X + 8.0f, CamPill.Y + 6.0f }, 10.6f, C.TextSoft, QueryCameraLabel(Session.CameraPreset));
    Surface.TextRightAligned({ CamPill.X, CamPill.Y + 6.0f, CamPill.Width - 8.0f, 12.0f }, 9.0f, C.TextMuted, "v");
    if (CamHov && Pointer.Pressed)
    {
        const uint32_t Next = (static_cast<uint32_t>(Session.CameraPreset) + 1u) % 4u;
        Session.ApplyCameraPreset(static_cast<FluidCameraPreset>(Next));
    }

    // Right-aligned toolbar controls
    float RX = TopBar.Right() - 14.0f;

    const Rectangle2D FocusBtn { RX - 24.0f, TopCY - 12.0f, 24.0f, 24.0f };
    const bool FocusHov = ControlAppearance::QueryPointerOver(FocusBtn, Pointer);
    Surface.FillCircle(FocusBtn.Center(), 11.0f, FocusHov ? C.CardElevated : ColorRgba { 0u, 0u, 0u, 0u });
    Surface.StrokeCircle(FocusBtn.Center(), 5.0f, FocusHov ? C.TextPrimary : C.TextMuted, 1.1f);
    Surface.FillCircle(FocusBtn.Center(), 1.5f, FocusHov ? C.TextPrimary : C.TextMuted);
    if (FocusHov && Pointer.Pressed) Session.ApplyCameraPreset(FluidCameraPreset::Perspective);
    RX = FocusBtn.X - 6.0f;

    const Rectangle2D BoundsBtn { RX - 24.0f, TopCY - 12.0f, 24.0f, 24.0f };
    LastBoundsBtnBounds = BoundsBtn;
    const bool BoundsHov = ControlAppearance::QueryPointerOver(BoundsBtn, Pointer);
    Surface.FillCircle(BoundsBtn.Center(), 11.0f, Session.ShowDomainBounds ? ColorRgba { 44u, 36u, 28u, 255u } : (BoundsHov ? C.CardElevated : ColorRgba { 0u, 0u, 0u, 0u }));
    Surface.StrokeRectangle({ BoundsBtn.Center().X - 5.0f, BoundsBtn.Center().Y - 5.0f, 10.0f, 10.0f },
        Session.ShowDomainBounds ? C.WarmAccent : C.TextMuted, 2.0f, 1.1f);
    if (BoundsHov && Pointer.Pressed) Session.ShowDomainBounds = !Session.ShowDomainBounds;
    RX = BoundsBtn.X - 8.0f;

    const Rectangle2D ChanPill { RX - 104.0f, TopCY - 12.0f, 104.0f, 24.0f };
    const bool ChanHov = ControlAppearance::QueryPointerOver(ChanPill, Pointer);
    Surface.FillRectangle(ChanPill, ChanHov ? C.CardElevated : C.CardBg, 12.0f);
    Surface.StrokeRectangle(ChanPill, C.Hairline, 12.0f, 0.9f);
    Surface.Text({ ChanPill.X + 10.0f, ChanPill.Y + 6.0f }, 10.2f, C.TextSoft, QueryChannelLabel(Doc.Shading.RenderChannel));
    Surface.TextRightAligned({ ChanPill.X, ChanPill.Y + 6.0f, ChanPill.Width - 8.0f, 12.0f }, 9.0f, C.TextMuted, "v");
    if (ChanHov && Pointer.Pressed)
    {
        const uint32_t Next = (static_cast<uint32_t>(Doc.Shading.RenderChannel) + 1u) % 9u;
        Doc.Shading.RenderChannel = static_cast<SimulationSpace::FluidSpace::FluidRenderChannel>(Next);
    }
    RX = ChanPill.X - 8.0f;

    const Rectangle2D DiagBtn { RX - 24.0f, TopCY - 12.0f, 24.0f, 24.0f };
    LastDiagButtonBounds = DiagBtn;
    const bool DiagHov = ControlAppearance::QueryPointerOver(DiagBtn, Pointer);
    Surface.FillCircle(DiagBtn.Center(), 11.0f, Session.ShowDiagnostics ? ColorRgba { 38u, 38u, 42u, 255u } : (DiagHov ? C.CardElevated : ColorRgba { 0u, 0u, 0u, 0u }));
    Surface.StrokeRectangle({ DiagBtn.Center().X - 5.0f, DiagBtn.Center().Y - 4.0f, 10.0f, 8.0f },
        Session.ShowDiagnostics ? C.TextPrimary : C.TextMuted, 1.5f, 1.1f);
    if (DiagHov && Pointer.Pressed) Session.ShowDiagnostics = !Session.ShowDiagnostics;
    RX = DiagBtn.X - 8.0f;

    const Rectangle2D BakeBtn { RX - 62.0f, TopCY - 12.0f, 62.0f, 24.0f };
    LastBakeButtonBounds = BakeBtn;
    const bool BakeHov = ControlAppearance::QueryPointerOver(BakeBtn, Pointer);
    Surface.FillRectangle(BakeBtn, Session.FlipbookModalOpen ? C.CardElevated : (BakeHov ? C.CardElevated : C.CardBg), 12.0f);
    Surface.StrokeRectangle(BakeBtn, C.Hairline, 12.0f, 0.9f);
    Surface.TextCentered(BakeBtn, 10.2f, C.TextPrimary, "Bake");
    if (BakeHov && Pointer.Pressed) Session.FlipbookModalOpen = !Session.FlipbookModalOpen;

    //--------------------------------------------------------------------------------------------------------------------
    // 2. Interactive Orbit / Brush / Detonate inside Viewport Surface
    //--------------------------------------------------------------------------------------------------------------------
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
        Session.Camera.PitchRadians = std::clamp(Session.Camera.PitchRadians + DY * 0.008f, -0.35f, 1.38f);
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

    //--------------------------------------------------------------------------------------------------------------------
    // 3. 3D Perspective Floor Grid + Wireframe Domain Bounds + Volumetric Fire/Smoke
    //--------------------------------------------------------------------------------------------------------------------
    Surface.PushClip(LastSurfaceBounds);

    // Subtle vertical gradient sky/void backdrop matching Html_WorkspaceUI_Default.png
    constexpr uint32_t BgBands = 16u;
    const float BandH = LastSurfaceBounds.Height / static_cast<float>(BgBands);
    for (uint32_t B = 0u; B < BgBands; ++B)
    {
        const float T = static_cast<float>(B) / static_cast<float>(BgBands - 1u);
        const auto R8 = static_cast<uint8_t>(26.0f + T * 18.0f);
        const auto G8 = static_cast<uint8_t>(30.0f + T * 20.0f);
        const auto B8 = static_cast<uint8_t>(38.0f + T * 24.0f);
        Surface.FillRectangle(
            { LastSurfaceBounds.X, LastSurfaceBounds.Y + static_cast<float>(B) * BandH, LastSurfaceBounds.Width, BandH + 0.6f },
            { R8, G8, B8, 255u }, 0.0f);
    }

    const float CosP = std::cos(Session.Camera.PitchRadians);
    const Mathematics::Axis3D Eye {
        Session.Camera.TargetWorld.X + Session.Camera.DistanceMeters * CosP * std::cos(Session.Camera.YawRadians),
        Session.Camera.TargetWorld.Y + Session.Camera.DistanceMeters * CosP * std::sin(Session.Camera.YawRadians),
        Session.Camera.TargetWorld.Z + Session.Camera.DistanceMeters * std::sin(Session.Camera.PitchRadians),
    };
    const Mathematics::Axis3D Forward = Mathematics::NormalizeAxis(Session.Camera.TargetWorld - Eye);
    const Mathematics::Axis3D Right   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Forward, { 0.0f, 0.0f, 1.0f }));
    const Mathematics::Axis3D Up      = Mathematics::EvaluateCross(Right, Forward);
    const float Aspect = LastSurfaceBounds.Width / std::max(1.0f, LastSurfaceBounds.Height);
    const float TanY   = 0.44f;
    const float TanX   = TanY * Aspect;

    auto ProjectPoint = [&](Mathematics::Axis3D W, Point2D& OutPt) noexcept -> bool {
        const Mathematics::Axis3D D = W - Eye;
        const float Zc = Mathematics::EvaluateDot(D, Forward);
        if (Zc <= 0.15f) return false;
        const float Xc = Mathematics::EvaluateDot(D, Right);
        const float Yc = Mathematics::EvaluateDot(D, Up);
        const float NdcX = Xc / (Zc * TanX);
        const float NdcY = Yc / (Zc * TanY);
        OutPt.X = LastSurfaceBounds.X + (NdcX * 0.5f + 0.5f) * LastSurfaceBounds.Width;
        OutPt.Y = LastSurfaceBounds.Y + (0.5f - NdcY * 0.5f) * LastSurfaceBounds.Height;
        return true;
    };

    // Highlighted center emitter pad on floor Z=0
    const float HalfX = Doc.Domain.BoundsWidthMeters * 0.5f;
    const float HalfY = Doc.Domain.BoundsWidthMeters * 0.5f;
    {
        Point2D P0 {}, P1 {}, P2 {}, P3 {};
        if (ProjectPoint({ -HalfX, -HalfY, 0.0f }, P0) && ProjectPoint({ HalfX, -HalfY, 0.0f }, P1) &&
            ProjectPoint({  HalfX,  HalfY, 0.0f }, P2) && ProjectPoint({ -HalfX, HalfY, 0.0f }, P3))
        {
            const std::array<Point2D, 4> Pad = {{ P0, P1, P2, P3 }};
            Surface.FillPolygon(Pad, { 255u, 255u, 255u, 14u });
        }
    }

    // Crisp perspective grid lines (-4..+4 m) + red X / blue Y center axes
    for (int G = -5; G <= 5; ++G)
    {
        const float Coord = static_cast<float>(G);
        Point2D A0 {}, A1 {}, B0 {}, B1 {};
        if (ProjectPoint({ Coord, -5.0f, 0.0f }, A0) && ProjectPoint({ Coord, 5.0f, 0.0f }, A1))
        {
            const ColorRgba Col = (G == 0) ? ColorRgba { 110u, 155u, 225u, 150u } : ColorRgba { 255u, 255u, 255u, 26u };
            Surface.StrokeLine(A0, A1, Col, (G == 0) ? 1.3f : 0.9f);
        }
        if (ProjectPoint({ -5.0f, Coord, 0.0f }, B0) && ProjectPoint({ 5.0f, Coord, 0.0f }, B1))
        {
            const ColorRgba Col = (G == 0) ? ColorRgba { 215u, 112u, 118u, 150u } : ColorRgba { 255u, 255u, 255u, 26u };
            Surface.StrokeLine(B0, B1, Col, (G == 0) ? 1.3f : 0.9f);
        }
    }

    // Optional projected 3D Hero Domain Wireframe Bounds (matching Html_Inspector_Rendering_Diagnostics.png!)
    const auto& Hero = Session.InspectSimulation().InspectHeroGrid();
    if (Session.ShowDomainBounds)
    {
        const auto& Mn = Hero.BoxMinWorld;
        const auto& Mx = Hero.BoxMaxWorld;
        const std::array<Mathematics::Axis3D, 8> Corners {{
            { Mn.X, Mn.Y, Mn.Z }, { Mx.X, Mn.Y, Mn.Z }, { Mx.X, Mx.Y, Mn.Z }, { Mn.X, Mx.Y, Mn.Z },
            { Mn.X, Mn.Y, Mx.Z }, { Mx.X, Mn.Y, Mx.Z }, { Mx.X, Mx.Y, Mx.Z }, { Mn.X, Mx.Y, Mx.Z },
        }};
        std::array<Point2D, 8> P {};
        bool AllOk = true;
        for (std::size_t I = 0u; I < 8u; ++I) AllOk &= ProjectPoint(Corners[I], P[I]);
        if (AllOk)
        {
            constexpr std::array<std::pair<int, int>, 12> Edges {{
                {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
            }};
            for (const auto& E : Edges)
            {
                Surface.StrokeLine(P[E.first], P[E.second], { 239u, 165u, 111u, 200u }, 1.2f);
            }
        }
    }

    // Volumetric Fire/Smoke + Soft-Glow Circular Embers/Ash/Debris composited cleanly via bilinear ImTextureData
    Session.RenderPreviewFramebuffer(280u, 210u);
    UploadPreviewTexture(Session.PreviewRgba8, Session.PreviewWidthPx, Session.PreviewHeightPx);
    if (PreviewTexture.Pixels != nullptr)
    {
        Surface.FillTexturedRectangle(LastSurfaceBounds, &PreviewTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // 4. Viewport HUD Overlays (Top-left Caption, Top-right FPS/Grid, Left Vertical Tool Pill, Bottom Help & Axis Gizmo)
    //--------------------------------------------------------------------------------------------------------------------
    // Top-left Live/Paused pill + object name
    const Rectangle2D LivePill { LastSurfaceBounds.X + 14.0f, LastSurfaceBounds.Y + 14.0f, 64.0f, 20.0f };
    Surface.FillRectangle(LivePill, { 16u, 18u, 22u, 210u }, 10.0f);
    Surface.FillCircle({ LivePill.X + 12.0f, LivePill.Center().Y }, 3.2f, Doc.Domain.Paused ? C.WarmAccent : C.LiveGreen);
    Surface.Text({ LivePill.X + 20.0f, LivePill.Y + 5.0f }, 9.0f, C.TextPrimary, Doc.Domain.Paused ? "PAUSED" : "LIVE");
    Surface.Text({ LivePill.Right() + 10.0f, LivePill.Y + 4.5f }, 11.2f, C.TextPrimary, "Fire emitter");
    Surface.Text({ LivePill.X + 2.0f, LivePill.Bottom() + 5.0f }, 9.4f, C.TextMuted, "Eulerian volume · physically shaded");

    // Top-right FPS & Grid HUD pills
    char GridBuf[24] {};
    std::snprintf(GridBuf, sizeof(GridBuf), "%u^3 GRID", Hero.Resolution);
    const Rectangle2D GridPill { LastSurfaceBounds.Right() - 86.0f, LastSurfaceBounds.Y + 14.0f, 72.0f, 22.0f };
    const Rectangle2D FpsPill  { GridPill.X - 62.0f, LastSurfaceBounds.Y + 14.0f, 56.0f, 22.0f };
    Surface.FillRectangle(FpsPill, { 16u, 18u, 22u, 205u }, 11.0f);
    Surface.StrokeRectangle(FpsPill, C.Hairline, 11.0f, 0.8f);
    Surface.TextCentered(FpsPill, 9.2f, C.TextSoft, "60 FPS");
    Surface.FillRectangle(GridPill, { 16u, 18u, 22u, 205u }, 11.0f);
    Surface.StrokeRectangle(GridPill, C.Hairline, 11.0f, 0.8f);
    Surface.TextCentered(GridPill, 9.2f, C.TextSoft, GridBuf);

    // Left floating vertical tool pill (Orbit | Brush | Burst | Slice | Turntable)
    const Rectangle2D ToolRail { LastSurfaceBounds.X + 14.0f, LastSurfaceBounds.Y + 86.0f, 36.0f, 182.0f };
    Surface.FillRectangle(ToolRail, { 18u, 20u, 24u, 220u }, 18.0f);
    Surface.StrokeRectangle(ToolRail, C.Hairline, 18.0f, 0.9f);
    constexpr std::array<FluidViewportTool, 4> ToolOrder {{
        FluidViewportTool::OrbitCamera,
        FluidViewportTool::PyroBrush,
        FluidViewportTool::DetonateBlast,
        FluidViewportTool::CrossSection,
    }};
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        const Rectangle2D TB { ToolRail.X + 4.0f, ToolRail.Y + 6.0f + static_cast<float>(I) * 34.0f, 28.0f, 28.0f };
        const bool Act = (Session.ActiveTool == ToolOrder[I]);
        const bool Hov = ControlAppearance::QueryPointerOver(TB, Pointer);
        if (Act || Hov)
        {
            Surface.FillCircle(TB.Center(), 13.0f, Act ? ColorRgba { 232u, 232u, 235u, 255u } : C.CardElevated);
        }
        DrawToolIcon(Surface, TB.Center(), I, Act ? ColorRgba { 16u, 16u, 18u, 255u } : C.TextSoft);
        if (Hov && Pointer.Pressed)
        {
            Session.ActiveTool = ToolOrder[I];
            if (ToolOrder[I] == FluidViewportTool::CrossSection)
            {
                Doc.Shading.RenderChannel =
                    (Doc.Shading.RenderChannel == SimulationSpace::FluidSpace::FluidRenderChannel::CrossSection)
                        ? SimulationSpace::FluidSpace::FluidRenderChannel::LitVolume
                        : SimulationSpace::FluidSpace::FluidRenderChannel::CrossSection;
            }
        }
    }
    Surface.FillRectangle({ ToolRail.X + 8.0f, ToolRail.Y + 142.0f, 20.0f, 1.0f }, C.Hairline, 0.0f);
    const Rectangle2D TurnBtn { ToolRail.X + 4.0f, ToolRail.Y + 148.0f, 28.0f, 28.0f };
    const bool TurnHov = ControlAppearance::QueryPointerOver(TurnBtn, Pointer);
    if (Session.TurntableEnabled || TurnHov)
    {
        Surface.FillCircle(TurnBtn.Center(), 13.0f, Session.TurntableEnabled ? C.WarmAccent : C.CardElevated);
    }
    DrawToolIcon(Surface, TurnBtn.Center(), 4u, Session.TurntableEnabled ? ColorRgba { 16u, 16u, 18u, 255u } : C.TextSoft);
    if (TurnHov && Pointer.Pressed) Session.TurntableEnabled = !Session.TurntableEnabled;

    // Bottom-left interaction help text
    Surface.Text({ LastSurfaceBounds.X + 16.0f, LastSurfaceBounds.Bottom() - 22.0f }, 9.4f, { 185u, 192u, 204u, 190u },
        "Drag to orbit  ·  Right-drag to pan  ·  Scroll to zoom");

    // Bottom-right 3D Axis Gizmo
    const Point2D GizC { LastSurfaceBounds.Right() - 40.0f, LastSurfaceBounds.Bottom() - 36.0f };
    Surface.StrokeLine(GizC, { GizC.X, GizC.Y - 22.0f }, { 92u, 196u, 128u, 230u }, 1.5f);
    Surface.StrokeLine(GizC, { GizC.X - 18.0f, GizC.Y + 11.0f }, { 222u, 96u, 96u, 230u }, 1.5f);
    Surface.StrokeLine(GizC, { GizC.X + 18.0f, GizC.Y + 11.0f }, { 98u, 160u, 238u, 230u }, 1.5f);
    Surface.FillCircle(GizC, 2.5f, C.TextPrimary);
    Surface.Text({ GizC.X - 3.5f, GizC.Y - 33.0f }, 8.8f, { 92u, 196u, 128u, 255u }, "Z");
    Surface.Text({ GizC.X - 27.0f, GizC.Y + 9.0f }, 8.8f, { 222u, 96u, 96u, 255u }, "X");
    Surface.Text({ GizC.X + 20.0f, GizC.Y + 9.0f }, 8.8f, { 98u, 160u, 238u, 255u }, "Y");

    // Floating Live Diagnostics card (matching Html_Inspector_Rendering_Diagnostics.png!)
    if (Session.ShowDiagnostics)
    {
        const Rectangle2D DiagBox { LastSurfaceBounds.Right() - 246.0f, LastSurfaceBounds.Y + 46.0f, 232.0f, 236.0f };
        Surface.FillRectangle(DiagBox, { 18u, 19u, 22u, 240u }, 16.0f);
        Surface.StrokeRectangle(DiagBox, C.HairlineStrong, 16.0f, 1.0f);

        Surface.Text({ DiagBox.X + 14.0f, DiagBox.Y + 12.0f }, 9.8f, C.TextSoft, "LIVE DIAGNOSTICS");
        const Rectangle2D CloseDiag { DiagBox.Right() - 28.0f, DiagBox.Y + 9.0f, 20.0f, 20.0f };
        if (ControlAppearance::QueryPointerOver(CloseDiag, Pointer) && Pointer.Pressed)
        {
            Session.ShowDiagnostics = false;
        }
        Surface.TextCentered(CloseDiag, 10.0f, C.TextMuted, "x");

        float DY = DiagBox.Y + 36.0f;
        auto DiagRow = [&](const char* K, const char* V) {
            Surface.Text({ DiagBox.X + 14.0f, DY }, 9.6f, C.TextMuted, K);
            Surface.TextRightAligned({ DiagBox.X, DY, DiagBox.Width - 14.0f, 14.0f }, 9.8f, C.TextPrimary, V);
            DY += 18.0f;
        };
        char GridStr[24] {}, StepsStr[24] {}, LightsStr[24] {};
        std::snprintf(GridStr, sizeof(GridStr), "%u^3", Hero.Resolution);
        std::snprintf(StepsStr, sizeof(StepsStr), "%llu", static_cast<unsigned long long>(Session.InspectSimulation().QueryStepCount()));
        std::snprintf(LightsStr, sizeof(LightsStr), "%zu", Session.InspectSimulation().InspectEmissiveLights().size());
        DiagRow("Backend", "CPU / Vulkan");
        DiagRow("Voxel grid", GridStr);
        DiagRow("Ray / shadow", "48 / 12");
        DiagRow("Sim steps", StepsStr);
        DiagRow("GI fire lights", LightsStr);

        DY += 8.0f;
        Surface.FillRectangle({ DiagBox.X + 14.0f, DY, DiagBox.Width - 28.0f, 1.0f }, C.Hairline, 0.0f);
        DY += 10.0f;

        Surface.Text({ DiagBox.X + 14.0f, DY + 4.0f }, 9.6f, C.TextMuted, "Debug field");
        Surface.TextRightAligned({ DiagBox.X, DY + 4.0f, DiagBox.Width - 14.0f, 14.0f }, 9.8f, C.TextPrimary, QueryChannelLabel(Doc.Shading.RenderChannel));
        DY += 24.0f;

        Surface.Text({ DiagBox.X + 14.0f, DY + 2.0f }, 9.6f, C.TextMuted, "Atlas minimap");
        const Rectangle2D CheckBox { DiagBox.Right() - 32.0f, DY, 18.0f, 18.0f };
        Surface.FillRectangle(CheckBox, Session.AtlasMinimapOpen ? C.TextPrimary : C.WellBg, 4.0f);
        Surface.StrokeRectangle(CheckBox, C.HairlineStrong, 4.0f, 1.0f);
        if (Session.AtlasMinimapOpen) Surface.TextCentered(CheckBox, 10.0f, { 16u, 16u, 18u, 255u }, "v");
        if (ControlAppearance::QueryPointerOver(CheckBox, Pointer) && Pointer.Pressed)
        {
            Session.AtlasMinimapOpen = !Session.AtlasMinimapOpen;
        }
    }

    Surface.PopClip();

    //--------------------------------------------------------------------------------------------------------------------
    // 5. Transport Bar (Rewind | Play | Step | Timecode | Speed | Live/uncached ... Burst pattern | Trigger D)
    //--------------------------------------------------------------------------------------------------------------------
    Surface.FillRectangle({ TransBar.X + 12.0f, TransBar.Y, TransBar.Width - 24.0f, 1.0f }, C.Hairline, 0.0f);
    float BX = TransBar.X + 16.0f;
    const float TransCY = TransBar.Center().Y;

    // Rewind
    const Rectangle2D ResetBtn { BX, TransCY - 12.0f, 24.0f, 24.0f };
    if (ControlAppearance::QueryPointerOver(ResetBtn, Pointer) && Pointer.Pressed) Session.TriggerReset();
    Surface.TextCentered(ResetBtn, 10.5f, C.TextSoft, "|<");
    BX += 30.0f;

    // Play/Pause circle
    const Rectangle2D PlayBtn { BX, TransCY - 15.0f, 30.0f, 30.0f };
    Surface.FillCircle(PlayBtn.Center(), 14.5f, C.TextPrimary);
    Surface.TextCentered(PlayBtn, 10.5f, { 16u, 16u, 18u, 255u }, Doc.Domain.Paused ? ">" : "||");
    if (ControlAppearance::QueryPointerOver(PlayBtn, Pointer) && Pointer.Pressed) Doc.Domain.Paused = !Doc.Domain.Paused;
    BX += 36.0f;

    // Step
    const Rectangle2D StepBtn { BX, TransCY - 12.0f, 24.0f, 24.0f };
    if (ControlAppearance::QueryPointerOver(StepBtn, Pointer) && Pointer.Pressed)
    {
        const bool WasPaused = Doc.Domain.Paused;
        Doc.Domain.Paused = false;
        Session.StepSimulation(1.0 / 60.0);
        Doc.Domain.Paused = WasPaused;
    }
    Surface.TextCentered(StepBtn, 10.5f, C.TextSoft, ">|");
    BX += 34.0f;

    // Timecode
    const double Sec = Session.InspectSimulation().QueryElapsedSeconds();
    const int Mins   = static_cast<int>(Sec) / 60;
    const double Rem = Sec - static_cast<double>(Mins * 60);
    char TimeBuf[24] {};
    std::snprintf(TimeBuf, sizeof(TimeBuf), "%02d:%06.3f", Mins, Rem);
    Surface.Text({ BX, TransCY - 6.0f }, 11.2f, C.TextPrimary, TimeBuf);
    BX += 66.0f;

    Surface.FillRectangle({ BX, TransCY - 8.0f, 1.0f, 16.0f }, C.Hairline, 0.0f);
    BX += 10.0f;

    // Speed cycle pill
    char SpeedBuf[16] {};
    std::snprintf(SpeedBuf, sizeof(SpeedBuf), "%.2gx", Session.PlaybackSpeed);
    const Rectangle2D SpeedPill { BX, TransCY - 11.0f, 44.0f, 22.0f };
    const bool SpdHov = ControlAppearance::QueryPointerOver(SpeedPill, Pointer);
    if (SpdHov) Surface.FillRectangle(SpeedPill, C.CardElevated, 11.0f);
    Surface.TextCentered(SpeedPill, 10.0f, C.TextSoft, SpeedBuf);
    if (SpdHov && Pointer.Pressed)
    {
        if (Session.PlaybackSpeed < 0.4f)      Session.PlaybackSpeed = 0.5f;
        else if (Session.PlaybackSpeed < 0.8f) Session.PlaybackSpeed = 1.0f;
        else if (Session.PlaybackSpeed < 1.5f) Session.PlaybackSpeed = 2.0f;
        else                                   Session.PlaybackSpeed = 0.25f;
    }
    BX += 52.0f;

    Surface.FillCircle({ BX + 4.0f, TransCY }, 2.8f, C.LiveGreen);
    Surface.Text({ BX + 12.0f, TransCY - 5.5f }, 9.8f, C.TextMuted, "Live / uncached");

    // Right: Burst pattern dropdown + Trigger [D] button
    const Rectangle2D TriggerBtn { TransBar.Right() - 98.0f, TransCY - 14.0f, 84.0f, 28.0f };
    const bool TrigHov = ControlAppearance::QueryPointerOver(TriggerBtn, Pointer);
    Surface.FillRectangle(TriggerBtn, TrigHov ? C.CardElevated : C.CardBg, 14.0f);
    Surface.StrokeRectangle(TriggerBtn, C.HairlineStrong, 14.0f, 0.9f);
    Surface.TextCentered(TriggerBtn, 10.2f, C.TextPrimary, "Trigger  D");
    if (TrigHov && Pointer.Pressed) Session.TriggerBlastNow();

    const Rectangle2D BurstPatternBtn { TriggerBtn.X - 116.0f, TransCY - 12.0f, 108.0f, 24.0f };
    const bool BurstHov = ControlAppearance::QueryPointerOver(BurstPatternBtn, Pointer);
    if (BurstHov) Surface.FillRectangle(BurstPatternBtn, C.CardElevated, 12.0f);
    Surface.Text({ BurstPatternBtn.X + 8.0f, BurstPatternBtn.Y + 6.0f }, 10.0f, C.TextSoft, QueryBurstLabel(Session.SelectedBurstPattern));
    Surface.TextRightAligned({ BurstPatternBtn.X, BurstPatternBtn.Y + 6.0f, BurstPatternBtn.Width - 6.0f, 12.0f }, 9.0f, C.TextMuted, "v");
    if (BurstHov && Pointer.Pressed) Session.SelectedBurstPattern = (Session.SelectedBurstPattern + 1u) % 5u;

    //--------------------------------------------------------------------------------------------------------------------
    // 6. Simulation Ruler (SIMULATION / FRAME 0020 + 0s..30s ticks + Continuous emission track + playhead)
    //--------------------------------------------------------------------------------------------------------------------
    Surface.FillRectangle({ RulerBar.X + 12.0f, RulerBar.Y, RulerBar.Width - 24.0f, 1.0f }, C.Hairline, 0.0f);
    Surface.Text({ RulerBar.X + 18.0f, RulerBar.Y + 10.0f }, 8.8f, C.TextMuted, "SIMULATION");
    char FrameBuf[24] {};
    std::snprintf(FrameBuf, sizeof(FrameBuf), "FRAME %04llu", static_cast<unsigned long long>(Session.InspectSimulation().QueryStepCount()));
    Surface.Text({ RulerBar.X + 18.0f, RulerBar.Y + 24.0f }, 8.8f, C.TextSoft, FrameBuf);

    const Rectangle2D TrackRect { RulerBar.X + 106.0f, RulerBar.Y + 6.0f, RulerBar.Width - 122.0f, 34.0f };
    for (int S = 0; S <= 30; S += 5)
    {
        const float T = static_cast<float>(S) / 30.0f;
        const float X = TrackRect.X + T * (TrackRect.Width - 12.0f);
        Surface.FillRectangle({ X, TrackRect.Y + 14.0f, 1.0f, 18.0f }, C.Hairline, 0.0f);
        char SBuf[8] {};
        std::snprintf(SBuf, sizeof(SBuf), "%ds", S);
        Surface.Text({ X + 2.0f, TrackRect.Y + 1.0f }, 8.4f, C.TextMuted, SBuf);
    }

    const Rectangle2D EmitTrack { TrackRect.X, TrackRect.Y + 18.0f, TrackRect.Width, 14.0f };
    Surface.FillRectangle(EmitTrack, { 26u, 26u, 28u, 255u }, 7.0f);
    Surface.StrokeRectangle(EmitTrack, C.Hairline, 7.0f, 0.8f);
    Surface.Text({ EmitTrack.X + 12.0f, EmitTrack.Y + 2.5f }, 8.4f, C.TextSoft,
        Doc.Emitter.Enabled ? "Continuous emission" : "Burst / impulse mode");

    const float PlayheadFrac = std::fmod(static_cast<float>(Sec), 30.0f) / 30.0f;
    const float PlayX = TrackRect.X + PlayheadFrac * (TrackRect.Width - 12.0f);
    Surface.FillRectangle({ PlayX - 0.8f, TrackRect.Y + 12.0f, 1.6f, 22.0f }, C.TextPrimary, 0.8f);
    Surface.FillCircle({ PlayX, TrackRect.Y + 12.0f }, 3.2f, C.TextPrimary);
}

} // namespace Frontier::Workspaces
