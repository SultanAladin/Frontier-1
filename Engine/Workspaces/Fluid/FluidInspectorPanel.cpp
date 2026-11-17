/*============================================================================================================================================
                                                         FLUIDINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidInspectorPanel: Project Zero property cards, [value|unit] pill sliders, 3-tab Source/Simulation/Rendering & blackbody ramp

#include "Engine/Workspaces/Fluid/FluidInspectorPanel.h"
#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

using namespace ParticleAppearance;

[[nodiscard]] const char* QueryPaletteName(SimulationSpace::FluidSpace::FluidColorPalette Pal) noexcept
{
    using P = SimulationSpace::FluidSpace::FluidColorPalette;
    switch (Pal)
    {
    case P::PlanckianBlackbody:     return "Physical Planckian Blackbody";
    case P::CinemaNapalm:           return "Cinema Napalm";
    case P::ThermobaricWhiteHot:    return "Thermobaric White-Hot";
    case P::WildfireCopperEmerald:  return "Wildfire Copper-Emerald";
    case P::CherenkovBluePlasma:    return "Cherenkov Blue Plasma";
    case P::ArcaneVoidMagenta:      return "Arcane Void Magenta";
    }
    return "Physical Planckian Blackbody";
}

void DrawNodeIcon(PixelSpace& Surface, Point2D C, FluidOutlinerNode Node, ColorRgba Ink) noexcept
{
    if (Node == FluidOutlinerNode::EmitterSource || Node == FluidOutlinerNode::ParticleEmbers)
    {
        const std::array<Point2D, 5> Flame = {{
            { C.X, C.Y - 7.5f },
            { C.X + 5.2f, C.Y + 0.8f },
            { C.X + 3.0f, C.Y + 6.5f },
            { C.X - 3.6f, C.Y + 6.2f },
            { C.X - 4.8f, C.Y + 0.4f },
        }};
        Surface.StrokePolyline(Flame, Ink, 1.4f, true);
    }
    else if (Node == FluidOutlinerNode::GiLighting)
    {
        Surface.StrokeCircle(C, 4.2f, Ink, 1.3f);
        for (int K = 0; K < 4; ++K)
        {
            const float A = static_cast<float>(K) * 0.785398f;
            Surface.StrokeLine(
                { C.X - std::cos(A) * 7.2f, C.Y - std::sin(A) * 7.2f },
                { C.X + std::cos(A) * 7.2f, C.Y + std::sin(A) * 7.2f },
                Ink, 1.1f);
        }
    }
    else if (Node == FluidOutlinerNode::ForceFields)
    {
        RasterizeWindLinesGlyph(Surface, C, 6.5f, Ink);
    }
    else
    {
        Surface.StrokeRectangle({ C.X - 6.0f, C.Y - 6.0f, 12.0f, 12.0f }, Ink, 2.5f, 1.3f);
    }
}

void BeginPropertyCard(
    PixelSpace& Surface,
    Rectangle2D CardRect,
    const char* Title,
    const char* Tag) noexcept
{
    const auto C = QueryStudioColors();
    Surface.FillRectangle(CardRect, C.CardBg, 20.0f);
    Surface.StrokeRectangle(CardRect, C.Hairline, 20.0f, 1.0f);
    Surface.Text({ CardRect.X + 16.0f, CardRect.Y + 13.0f }, 13.5f, C.TextPrimary, Title);
    Surface.TextRightAligned({ CardRect.X, CardRect.Y + 15.0f, CardRect.Width - 16.0f, 14.0f }, 8.6f, C.TextMuted, Tag);
}

} // namespace

Rectangle2D FluidInspectorPanel::QueryTabBounds(FluidInspectorTab Tab) const noexcept
{
    const std::size_t Idx = static_cast<std::size_t>(Tab);
    return (Idx < LastTabRects.size()) ? LastTabRects[Idx] : Rectangle2D {};
}

bool FluidInspectorPanel::DrawSliderRow(
    PixelSpace&           Surface,
    Rectangle2D           RowRect,
    const char*           Label,
    const char*           UnitLabel,
    float&                InOutValue,
    float                 MinVal,
    float                 MaxVal,
    const ControlPointer& Pointer,
    uint32_t              SliderSlot,
    const char*           Format) noexcept
{
    const auto C = QueryStudioColors();
    Surface.Text({ RowRect.X, RowRect.Y }, 10.6f, C.TextSoft, Label);

    const float CtrlY = RowRect.Y + 18.0f;
    // Project Zero pill [ value | unit ] (92px total: 58px value + 34px unit)
    const Rectangle2D PillRect { RowRect.X, CtrlY, 92.0f, 26.0f };
    Surface.FillRectangle(PillRect, { 10u, 10u, 12u, 255u }, 13.0f);
    Surface.StrokeRectangle(PillRect, C.Hairline, 13.0f, 0.9f);
    Surface.FillRectangle({ PillRect.Right() - 34.0f, PillRect.Y + 4.0f, 1.0f, PillRect.Height - 8.0f }, C.Hairline, 0.0f);

    char ValBuf[32] {};
    std::snprintf(ValBuf, sizeof(ValBuf), Format, InOutValue);
    Surface.TextCentered({ PillRect.X + 2.0f, PillRect.Y, PillRect.Width - 36.0f, PillRect.Height }, 10.6f, C.TextPrimary, ValBuf);
    Surface.TextCentered({ PillRect.Right() - 33.0f, PillRect.Y, 31.0f, PillRect.Height }, 9.6f, C.TextMuted, UnitLabel);

    // Track to the right of the value pill
    const float TrackX = PillRect.Right() + 10.0f;
    const float TrackW = std::max(40.0f, RowRect.Right() - TrackX);
    const Rectangle2D Track { TrackX, CtrlY + 3.0f, TrackW, 20.0f };
    Surface.FillRectangle(Track, { 34u, 34u, 37u, 255u }, 10.0f);

    const float T = std::clamp((InOutValue - MinVal) / std::max(1.0e-4f, MaxVal - MinVal), 0.0f, 1.0f);
    const float FillW = std::max(18.0f, Track.Width * T);
    Surface.FillRectangle({ Track.X, Track.Y, FillW, Track.Height }, { 74u, 74u, 78u, 255u }, 10.0f);
    const float KnobX = std::clamp(Track.X + Track.Width * T, Track.X + 9.0f, Track.Right() - 9.0f);
    Surface.FillCircle({ KnobX, Track.Center().Y }, 8.0f, { 232u, 232u, 234u, 255u });

    if (Pointer.Pressed && Track.InsetXY(-4.0f, -4.0f).Contains({ Pointer.X, Pointer.Y }))
    {
        ActiveDragSlider = static_cast<int32_t>(SliderSlot);
    }
    if (!Pointer.Down)
    {
        ActiveDragSlider = -1;
    }
    if (ActiveDragSlider == static_cast<int32_t>(SliderSlot) && Pointer.Down)
    {
        const float NewT = std::clamp((Pointer.X - Track.X) / std::max(1.0f, Track.Width), 0.0f, 1.0f);
        InOutValue = MinVal + NewT * (MaxVal - MinVal);
        return true;
    }
    return false;
}

bool FluidInspectorPanel::DrawToggleRow(
    PixelSpace&           Surface,
    Rectangle2D           RowRect,
    const char*           Label,
    bool&                 InOutChecked,
    const ControlPointer& Pointer) noexcept
{
    const auto C = QueryStudioColors();
    Surface.Text({ RowRect.X, RowRect.Y + 4.0f }, 10.8f, C.TextSoft, Label);
    const Rectangle2D Pill { RowRect.Right() - 42.0f, RowRect.Y + 1.0f, 42.0f, 22.0f };
    Surface.FillRectangle(Pill, InOutChecked ? ColorRgba { 235u, 235u, 238u, 255u } : ColorRgba { 38u, 38u, 42u, 255u }, 11.0f);
    Surface.FillCircle(
        { InOutChecked ? (Pill.Right() - 11.0f) : (Pill.X + 11.0f), Pill.Center().Y },
        8.0f,
        InOutChecked ? ColorRgba { 16u, 16u, 18u, 255u } : ColorRgba { 160u, 160u, 165u, 255u });
    if (Pointer.Pressed && Pill.Contains({ Pointer.X, Pointer.Y }))
    {
        InOutChecked = !InOutChecked;
        return true;
    }
    return false;
}

void FluidInspectorPanel::Render(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer,
    float                  Opacity) noexcept
{
    const auto C = QueryStudioColors();
    LastTabRects.fill(Rectangle2D {});
    LastBlackbodyStripRect = Rectangle2D {};

    Surface.FillRectangle(Bounds, C.PanelBg.ScaledAlpha(Opacity), 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline.ScaledAlpha(Opacity), 22.0f, 1.0f);

    const float PadX   = 16.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 14.0f;

    // 1. Panel Header ("Inspector" + circular Reset button)
    Surface.Text({ Bounds.X + PadX, Y + 2.0f }, 14.5f, C.TextPrimary, "Inspector");
    const Rectangle2D ResetBtn { Bounds.Right() - PadX - 24.0f, Y, 24.0f, 24.0f };
    const bool ResetHov = ControlAppearance::QueryPointerOver(ResetBtn, Pointer);
    Surface.FillCircle(ResetBtn.Center(), 12.0f, ResetHov ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(ResetBtn.Center(), 12.0f, C.Hairline, 0.9f);
    Surface.StrokeCircle(ResetBtn.Center(), 4.5f, C.TextSoft, 1.1f);
    if (ResetHov && Pointer.Pressed)
    {
        Session.LoadPreset(Session.ActivePresetId);
    }
    Y += 34.0f;

    auto& Doc = Session.AccessSimulation().AccessDocument();

    // 2. Selected Entity Header Card
    const Rectangle2D EntityCard { Bounds.X + PadX, Y, InnerW, 64.0f };
    Surface.FillRectangle(EntityCard, C.CardBg, 18.0f);
    Surface.StrokeRectangle(EntityCard, C.Hairline, 18.0f, 1.0f);

    const Rectangle2D IconTile { EntityCard.X + 12.0f, EntityCard.Y + 12.0f, 40.0f, 40.0f };
    Surface.FillCircle(IconTile.Center(), 20.0f, { 28u, 28u, 32u, 255u });
    Surface.StrokeCircle(IconTile.Center(), 20.0f, C.Hairline, 0.9f);

    const char* EntityTitle = "Fire emitter";
    const char* EntityType  = "CONTINUOUS SOURCE";
    bool*       TogglePtr   = &Doc.Emitter.Enabled;
    ColorRgba   IconInk     = C.WarmAccent;

    switch (Session.SelectedNode)
    {
    case FluidOutlinerNode::HeroVolume:
        EntityTitle = "Gas domain";
        EntityType  = "EULERIAN VOLUME";
        TogglePtr   = &Doc.Domain.AdaptiveCeilingEnabled;
        IconInk     = ColorRgba { 98u, 174u, 240u, 255u };
        break;
    case FluidOutlinerNode::EmitterSource:
        EntityTitle = "Fire emitter";
        EntityType  = "CONTINUOUS SOURCE";
        TogglePtr   = &Doc.Emitter.Enabled;
        IconInk     = C.WarmAccent;
        break;
    case FluidOutlinerNode::GiLighting:
        EntityTitle = "Directional light";
        EntityType  = "GI & SHADOWS";
        TogglePtr   = &Doc.GiCoupling.EmitGiLights;
        IconInk     = ColorRgba { 240u, 198u, 96u, 255u };
        break;
    case FluidOutlinerNode::SceneObstacle:
        EntityTitle = "Scene collider";
        EntityType  = "ANALYTIC SDF";
        TogglePtr   = &Doc.Domain.WorldGridEnabled;
        IconInk     = ColorRgba { 170u, 178u, 192u, 255u };
        break;
    case FluidOutlinerNode::ForceFields:
        EntityTitle = "ForceFields wind";
        EntityType  = "VECTOR FIELD FORCE";
        TogglePtr   = &Doc.Combustion.CoupleForceFields;
        IconInk     = C.TealTint;
        break;
    case FluidOutlinerNode::WorldDriftGrid:
        EntityTitle = "World drift grid";
        EntityType  = "FAR-FIELD ADVECTION";
        TogglePtr   = &Doc.Domain.WorldGridEnabled;
        IconInk     = C.TealTint;
        break;
    case FluidOutlinerNode::ParticleEmbers:
        EntityTitle = "Embers & GI";
        EntityType  = "FLUID-COUPLED PARTICLES";
        TogglePtr   = &Doc.Particles.ShowEmbers;
        IconInk     = C.WarmAccent;
        break;
    }

    DrawNodeIcon(Surface, IconTile.Center(), Session.SelectedNode, IconInk);
    Surface.Text({ IconTile.Right() + 12.0f, EntityCard.Y + 13.0f }, 15.0f, C.TextPrimary, EntityTitle);
    Surface.Text({ IconTile.Right() + 12.0f, EntityCard.Y + 37.0f }, 8.6f,  C.TextMuted,   EntityType);

    if (TogglePtr != nullptr)
    {
        const Rectangle2D SwitchRect { EntityCard.Right() - 56.0f, EntityCard.Center().Y - 12.0f, 42.0f, 24.0f };
        Surface.FillRectangle(SwitchRect, *TogglePtr ? ColorRgba { 235u, 235u, 238u, 255u } : ColorRgba { 38u, 38u, 42u, 255u }, 12.0f);
        Surface.FillCircle(
            { *TogglePtr ? (SwitchRect.Right() - 12.0f) : (SwitchRect.X + 12.0f), SwitchRect.Center().Y },
            9.0f,
            *TogglePtr ? ColorRgba { 16u, 16u, 18u, 255u } : ColorRgba { 160u, 160u, 165u, 255u });
        if (Pointer.Pressed && SwitchRect.Contains({ Pointer.X, Pointer.Y }))
        {
            *TogglePtr = !(*TogglePtr);
            Session.DocumentDirty = true;
        }
    }
    Y += 74.0f;

    // 3. 3-Tab Segmented Pill: Source | Simulation | Rendering
    const Rectangle2D TabBar { Bounds.X + PadX, Y, InnerW, 30.0f };
    Surface.FillRectangle(TabBar, C.WellBg, 15.0f);
    Surface.StrokeRectangle(TabBar, C.Hairline, 15.0f, 0.9f);

    struct TabDef { FluidInspectorTab Tab; const char* Label; };
    constexpr std::array<TabDef, 3> Tabs {{
        { FluidInspectorTab::Source,     "Source" },
        { FluidInspectorTab::Simulation, "Simulation" },
        { FluidInspectorTab::Rendering,  "Rendering" },
    }};
    const float TabW = (TabBar.Width - 6.0f) / 3.0f;
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const Rectangle2D TR { TabBar.X + 3.0f + static_cast<float>(I) * TabW, TabBar.Y + 3.0f, TabW, 24.0f };
        LastTabRects[I] = TR;
        const bool Active = (Session.ActiveTab == Tabs[I].Tab);
        const bool Hov    = ControlAppearance::QueryPointerOver(TR, Pointer);
        if (Active || Hov)
        {
            Surface.FillRectangle(TR, Active ? C.CardElevated : ColorRgba { 24u, 24u, 26u, 255u }, 12.0f);
        }
        Surface.TextCentered(TR, 10.6f, Active ? C.TextPrimary : C.TextMuted, Tabs[I].Label);
        if (Hov && Pointer.Pressed)
        {
            Session.ActiveTab = Tabs[I].Tab;
        }
    }
    Y += 40.0f;

    const float FooterY = Bounds.Bottom() - 32.0f;
    bool NeedsReconfigure = false;
    bool Edited           = false;

    Surface.PushClip({ Bounds.X + PadX, Y, InnerW, std::max(40.0f, FooterY - Y - 4.0f) });

    if (Session.ActiveTab == FluidInspectorTab::Source)
    {
        // Card 1: Emission
        const Rectangle2D EmitCard { Bounds.X + PadX, Y, InnerW, 256.0f };
        BeginPropertyCard(Surface, EmitCard, "Emission", "SOURCE");
        const float CX = EmitCard.X + 16.0f;
        const float CW = EmitCard.Width - 32.0f;
        float CY = EmitCard.Y + 42.0f;

        Surface.Text({ CX, CY }, 9.2f, C.TextMuted, "Continuous source at domain X/Y centre.");
        CY += 22.0f;
        Edited |= DrawToggleRow(Surface, { CX, CY, CW, 24.0f }, "Continuous emission", Doc.Emitter.Enabled, Pointer);
        CY += 32.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Emission rate", "x", Doc.Emitter.Rate, 0.1f, 3.0f, Pointer, 0u);
        CY += 54.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Source radius", "m", Doc.Emitter.RadiusMeters, 0.04f, 0.60f, Pointer, 1u, "%.3f");
        CY += 54.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Source height", "m", Doc.Emitter.HeightMeters, 0.03f, 0.50f, Pointer, 2u);

        Y = EmitCard.Bottom() + 12.0f;

        // Card 2: Fuel & temperature
        const Rectangle2D FuelCard { Bounds.X + PadX, Y, InnerW, 224.0f };
        BeginPropertyCard(Surface, FuelCard, "Fuel & temperature", "INJECTION");
        CY = FuelCard.Y + 42.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Temperature injection", "-", Doc.Emitter.Temperature, 0.2f, 8.0f, Pointer, 3u);
        CY += 54.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Upward jet velocity", "m/s", Doc.Emitter.UpwardVelocityMps, 0.0f, 8.0f, Pointer, 4u);
        CY += 54.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Swirl vorticity", "rad", Doc.Emitter.Swirl, 0.0f, 6.0f, Pointer, 5u);
    }
    else if (Session.ActiveTab == FluidInspectorTab::Simulation)
    {
        // Card 1: Domain
        const Rectangle2D DomCard { Bounds.X + PadX, Y, InnerW, 272.0f };
        BeginPropertyCard(Surface, DomCard, "Domain", "3D");
        const float CX = DomCard.X + 16.0f;
        const float CW = DomCard.Width - 32.0f;
        float CY = DomCard.Y + 42.0f;

        NeedsReconfigure |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Width / depth", "m", Doc.Domain.BoundsWidthMeters, 1.0f, 6.0f, Pointer, 10u);
        CY += 54.0f;
        NeedsReconfigure |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Height", "m", Doc.Domain.BoundsHeightMeters, 1.0f, 8.0f, Pointer, 11u);
        CY += 54.0f;
        NeedsReconfigure |= DrawToggleRow(Surface, { CX, CY, CW, 24.0f }, "Expand around bursts", Doc.Domain.AdaptiveCeilingEnabled, Pointer);
        CY += 32.0f;
        NeedsReconfigure |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Expansion limit", "m", Doc.Domain.MaxExplosionCeilingMeters, 4.0f, 60.0f, Pointer, 12u);

        Y = DomCard.Bottom() + 12.0f;

        // Card 2: Voxel grid & Forces
        const Rectangle2D GridCard { Bounds.X + PadX, Y, InnerW, 250.0f };
        BeginPropertyCard(Surface, GridCard, "Voxel grid & Forces", "SOLVER");
        CY = GridCard.Y + 42.0f;
        Surface.Text({ CX, CY }, 9.2f, C.TextMuted, "Resolution rebuilds Hero grid; drift runs live.");
        CY += 20.0f;

        // Resolution selector pills (32^3 / 48^3 / 64^3)
        constexpr std::array<uint32_t, 3> ResOptions {{ 32u, 48u, 64u }};
        const float PillW = (CW - 12.0f) / 3.0f;
        for (uint32_t K = 0u; K < 3u; ++K)
        {
            const Rectangle2D RP { CX + static_cast<float>(K) * (PillW + 6.0f), CY, PillW, 26.0f };
            const bool Act = (Doc.Domain.GridResolution == ResOptions[K]);
            const bool Hov = ControlAppearance::QueryPointerOver(RP, Pointer);
            Surface.FillRectangle(RP, Act ? C.CardElevated : ColorRgba { 10u, 10u, 12u, 255u }, 13.0f);
            Surface.StrokeRectangle(RP, Act ? C.WarmAccent : C.Hairline, 13.0f, 0.9f);
            char RBuf[16] {};
            std::snprintf(RBuf, sizeof(RBuf), "%u^3", ResOptions[K]);
            Surface.TextCentered(RP, 10.2f, Act ? C.TextPrimary : C.TextMuted, RBuf);
            if (Hov && Pointer.Pressed)
            {
                Doc.Domain.GridResolution = ResOptions[K];
                NeedsReconfigure = true;
            }
        }
        CY += 36.0f;
        NeedsReconfigure |= DrawToggleRow(Surface, { CX, CY, CW, 24.0f }, "World smoke drift grid", Doc.Domain.WorldGridEnabled, Pointer);
        CY += 30.0f;
        Edited |= DrawToggleRow(Surface, { CX, CY, CW, 24.0f }, "Couple ForceFields wind", Doc.Combustion.CoupleForceFields, Pointer);
        CY += 32.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Buoyancy (+Z)", "m/s", Doc.Combustion.Buoyancy, 0.0f, 12.0f, Pointer, 13u);
    }
    else
    {
        // Card 1: Volume shading
        const Rectangle2D ShadeCard { Bounds.X + PadX, Y, InnerW, 336.0f };
        BeginPropertyCard(Surface, ShadeCard, "Volume shading", "VOLUME");
        const float CX = ShadeCard.X + 16.0f;
        const float CW = ShadeCard.Width - 32.0f;
        float CY = ShadeCard.Y + 42.0f;

        Surface.Text({ CX, CY }, 10.2f, C.TextSoft, "Colour palette (click to cycle)");
        CY += 18.0f;
        const Rectangle2D PalPill { CX, CY, CW, 28.0f };
        const bool PalHov = ControlAppearance::QueryPointerOver(PalPill, Pointer);
        Surface.FillRectangle(PalPill, { 10u, 10u, 12u, 255u }, 14.0f);
        Surface.StrokeRectangle(PalPill, PalHov ? C.WarmAccent : C.Hairline, 14.0f, 0.9f);
        Surface.Text({ PalPill.X + 12.0f, PalPill.Y + 7.5f }, 10.2f, C.TextPrimary, QueryPaletteName(Doc.Shading.ColorPalette));
        Surface.TextRightAligned({ PalPill.X, PalPill.Y + 7.5f, PalPill.Width - 12.0f, 14.0f }, 9.5f, C.TextMuted, "v");
        if (PalHov && Pointer.Pressed)
        {
            const uint32_t Next = (static_cast<uint32_t>(Doc.Shading.ColorPalette) + 1u) % 6u;
            Doc.Shading.ColorPalette = static_cast<SimulationSpace::FluidSpace::FluidColorPalette>(Next);
            Edited = true;
        }
        CY += 34.0f;

        // Live Blackbody Palette Ramp Strip
        const Rectangle2D Strip { CX, CY, CW, 12.0f };
        LastBlackbodyStripRect = Strip;
        constexpr uint32_t Slices = 28u;
        const float SW = Strip.Width / static_cast<float>(Slices);
        for (uint32_t S = 0u; S < Slices; ++S)
        {
            const float Temp = (static_cast<float>(S) / static_cast<float>(Slices - 1u)) * 4.5f;
            const auto Col = SimulationSpace::FluidSpace::FluidPresets::EvaluateBlackbodyPalette(
                Temp, Doc.Shading.TemperatureScale, Doc.Shading.ColorPalette);
            const auto R = static_cast<uint8_t>(std::clamp(Col.R / (1.0f + Col.R) * 255.0f, 0.0f, 255.0f));
            const auto G = static_cast<uint8_t>(std::clamp(Col.G / (1.0f + Col.G) * 255.0f, 0.0f, 255.0f));
            const auto B = static_cast<uint8_t>(std::clamp(Col.B / (1.0f + Col.B) * 255.0f, 0.0f, 255.0f));
            Surface.FillRectangle({ Strip.X + static_cast<float>(S) * SW, Strip.Y, SW + 0.6f, Strip.Height }, { R, G, B, 255u }, 0.0f);
        }
        Surface.StrokeRectangle(Strip, C.Hairline, 4.0f, 0.9f);
        if (Pointer.Pressed && Strip.Contains({ Pointer.X, Pointer.Y }))
        {
            const uint32_t Next = (static_cast<uint32_t>(Doc.Shading.ColorPalette) + 1u) % 6u;
            Doc.Shading.ColorPalette = static_cast<SimulationSpace::FluidSpace::FluidColorPalette>(Next);
            Edited = true;
        }
        CY += 20.0f;

        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Fire intensity", "-", Doc.Shading.FireIntensity, 0.0f, 8.0f, Pointer, 20u);
        CY += 54.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Smoke extinction", "-", Doc.Shading.DensityExtinction, 0.2f, 24.0f, Pointer, 21u);
        CY += 54.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Smoke albedo", "-", Doc.Shading.SmokeAlbedo, 0.05f, 1.0f, Pointer, 22u);
        CY += 54.0f;
        Edited |= DrawSliderRow(Surface, { CX, CY, CW, 46.0f }, "Exposure", "-", Doc.Shading.Exposure, 0.25f, 3.0f, Pointer, 23u);

        Y = ShadeCard.Bottom() + 12.0f;

        // Card 2: Directional light & GI & Tornado Debris Array
        const Rectangle2D GiCard { Bounds.X + PadX, Y, InnerW, 198.0f };
        BeginPropertyCard(Surface, GiCard, "Directional light & GI", "LIGHT");
        CY = GiCard.Y + 42.0f;
        Edited |= DrawToggleRow(Surface, { CX, CY, CW, 24.0f }, "Emit GI fire lights", Doc.GiCoupling.EmitGiLights, Pointer);
        CY += 26.0f;
        Edited |= DrawToggleRow(Surface, { CX, CY, CW, 24.0f }, "Cast volumetric GI shadows", Doc.GiCoupling.CastGiShadows, Pointer);
        CY += 26.0f;
        Edited |= DrawToggleRow(Surface, { CX, CY, CW, 24.0f }, "Fluid-advected embers & debris", Doc.Particles.ShowEmbers, Pointer);
        CY += 28.0f;

        Surface.Text({ CX, CY }, 9.2f, C.TextMuted, "Tornado debris array (rock/twig/leaf/paper/wood/metal)");
        CY += 16.0f;
        constexpr std::array<SimulationSpace::FluidSpace::FluidDebrisKind, 6> AllKinds {{
            SimulationSpace::FluidSpace::FluidDebrisKind::RockChunk,
            SimulationSpace::FluidSpace::FluidDebrisKind::TwigStick,
            SimulationSpace::FluidSpace::FluidDebrisKind::Leaf,
            SimulationSpace::FluidSpace::FluidDebrisKind::PaperSheet,
            SimulationSpace::FluidSpace::FluidDebrisKind::WoodSplinter,
            SimulationSpace::FluidSpace::FluidDebrisKind::MetalScrap,
        }};
        const float ChipW = (CW - 10.0f) / 3.0f;
        for (uint32_t K = 0u; K < 6u; ++K)
        {
            const uint32_t Col = K % 3u;
            const uint32_t Row = K / 3u;
            const Rectangle2D CR { CX + static_cast<float>(Col) * (ChipW + 5.0f), CY + static_cast<float>(Row) * 24.0f, ChipW, 20.0f };
            const auto Kind = AllKinds[K];
            const auto It = std::find(Doc.Particles.DebrisTypes.begin(), Doc.Particles.DebrisTypes.end(), Kind);
            const bool Act = (It != Doc.Particles.DebrisTypes.end());
            const bool Hov = ControlAppearance::QueryPointerOver(CR, Pointer);
            Surface.FillRectangle(CR, Act ? C.CardElevated : ColorRgba { 10u, 10u, 12u, 255u }, 10.0f);
            Surface.StrokeRectangle(CR, Act ? C.WarmAccent : C.Hairline, 10.0f, 0.85f);
            Surface.TextCentered(CR, 9.0f, Act ? C.TextPrimary : C.TextMuted,
                                 SimulationSpace::FluidSpace::QueryFluidDebrisKindLabel(Kind));
            if (Hov && Pointer.Pressed)
            {
                if (Act)
                {
                    if (Doc.Particles.DebrisTypes.size() > 1u)
                    {
                        Doc.Particles.DebrisTypes.erase(It);
                    }
                }
                else
                {
                    Doc.Particles.DebrisTypes.push_back(Kind);
                }
                Edited = true;
            }
        }
    }

    Surface.PopClip();

    if (NeedsReconfigure)
    {
        Session.AccessSimulation().Configure(Doc);
        Session.DocumentDirty = true;
    }
    else if (Edited)
    {
        Session.DocumentDirty = true;
    }

    // Inspector Footer
    Surface.FillRectangle({ Bounds.X + PadX, FooterY - 2.0f, InnerW, 1.0f }, C.Hairline, 0.0f);
    Surface.Text({ Bounds.X + PadX + 4.0f, FooterY + 7.0f }, 9.4f, C.TextMuted, "Changes apply while rendering");
}

} // namespace Frontier::Workspaces
