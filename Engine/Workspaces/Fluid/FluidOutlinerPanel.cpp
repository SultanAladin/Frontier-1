/*============================================================================================================================================
                                                          FLUIDOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidOutlinerPanel: Project Zero Outliner (census tiles, search, filters, scene collection) + Preset library cards

#include "Engine/Workspaces/Fluid/FluidOutlinerPanel.h"
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

void DrawFlameGlyph(PixelSpace& Surface, Point2D C, float R, ColorRgba Ink) noexcept
{
    const std::array<Point2D, 5> Flame = {{
        { C.X,            C.Y - R * 1.1f },
        { C.X + R * 0.7f, C.Y + R * 0.1f },
        { C.X + R * 0.4f, C.Y + R * 0.9f },
        { C.X - R * 0.5f, C.Y + R * 0.85f },
        { C.X - R * 0.65f,C.Y + R * 0.05f },
    }};
    Surface.StrokePolyline(Flame, Ink, 1.3f, true);
}

void DrawCubeGlyph(PixelSpace& Surface, Point2D C, float R, ColorRgba Ink) noexcept
{
    Surface.StrokeRectangle({ C.X - R * 0.72f, C.Y - R * 0.72f, R * 1.44f, R * 1.44f }, Ink, 2.0f, 1.2f);
}

void DrawSunGlyph(PixelSpace& Surface, Point2D C, float R, ColorRgba Ink) noexcept
{
    Surface.StrokeCircle(C, R * 0.45f, Ink, 1.2f);
    for (int K = 0; K < 4; ++K)
    {
        const float A = static_cast<float>(K) * 0.785398f;
        const float Dx = std::cos(A) * R * 0.9f;
        const float Dy = std::sin(A) * R * 0.9f;
        Surface.StrokeLine({ C.X - Dx, C.Y - Dy }, { C.X + Dx, C.Y + Dy }, Ink, 1.0f);
    }
}

[[nodiscard]] bool MatchesPresetCategory(std::string_view Category, FluidPresetFilter Filter) noexcept
{
    if (Filter == FluidPresetFilter::All) return true;
    if (Filter == FluidPresetFilter::Fire)  return Category == "Fire" || Category == "Flame" || Category == "Plasma";
    if (Filter == FluidPresetFilter::Smoke) return Category == "Smoke" || Category == "Steam" || Category == "Dust";
    if (Filter == FluidPresetFilter::Blast) return Category == "Blast" || Category == "Explosion";
    return true;
}

} // namespace

Rectangle2D FluidOutlinerPanel::QueryLastNodeRowBounds(FluidOutlinerNode Node) const noexcept
{
    const std::size_t Idx = static_cast<std::size_t>(Node);
    return (Idx < LastNodeRowRects.size()) ? LastNodeRowRects[Idx] : Rectangle2D {};
}

Rectangle2D FluidOutlinerPanel::QueryLastPresetCardBounds(std::size_t Index) const noexcept
{
    return (Index < LastPresetCardRects.size()) ? LastPresetCardRects[Index] : Rectangle2D {};
}

void FluidOutlinerPanel::Render(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer,
    float                  Opacity) noexcept
{
    const auto C = QueryStudioColors();
    LastNodeRowRects.fill(Rectangle2D {});
    LastPresetCardRects.fill(Rectangle2D {});

    Surface.FillRectangle(Bounds, C.PanelBg.ScaledAlpha(Opacity), 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline.ScaledAlpha(Opacity), 22.0f, 1.0f);

    const float PadX   = 16.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 18.0f;

    const auto& Doc = Session.InspectSimulation().InspectDocument();

    // 1. Header: "Outliner" + subtitle + compact toggle
    Surface.Text({ Bounds.X + PadX, Y }, 19.0f, C.TextPrimary, "Outliner");
    const std::size_t VisibleNodeCount = Session.CompactOutliner ? 3u : 6u;
    char SubBuf[48] {};
    std::snprintf(SubBuf, sizeof(SubBuf), "Scene · %zu nodes", VisibleNodeCount);
    Surface.Text({ Bounds.X + PadX + 86.0f, Y + 6.0f }, 9.8f, C.TextMuted, SubBuf);

    const Rectangle2D CompactBtn { Bounds.Right() - PadX - 28.0f, Y + 2.0f, 28.0f, 28.0f };
    const bool CompactHov = ControlAppearance::QueryPointerOver(CompactBtn, Pointer);
    Surface.FillCircle(CompactBtn.Center(), 14.0f, CompactHov ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(CompactBtn.Center(), 14.0f, C.Hairline, 1.0f);
    Surface.StrokeLine({ CompactBtn.Center().X - 2.6f, CompactBtn.Center().Y - 4.0f }, { CompactBtn.Center().X - 2.6f, CompactBtn.Center().Y + 4.0f }, C.TextSoft, 1.2f);
    Surface.StrokeLine({ CompactBtn.Center().X + 2.6f, CompactBtn.Center().Y - 4.0f }, { CompactBtn.Center().X + 2.6f, CompactBtn.Center().Y + 4.0f }, C.TextSoft, 1.2f);
    if (CompactHov && Pointer.Pressed)
    {
        Session.CompactOutliner = !Session.CompactOutliner;
    }

    Y += 40.0f;

    // 2. Census Stat Tiles ("Enabled" / "Disabled")
    uint32_t EnabledCount  = 2u; // Gas domain + Directional light
    uint32_t DisabledCount = 0u;
    if (Doc.Emitter.Enabled) ++EnabledCount; else ++DisabledCount;
    if (Doc.Obstacle.Type != SimulationSpace::FluidSpace::FluidObstacleType::None) ++EnabledCount; else ++DisabledCount;
    if (Doc.Combustion.CoupleForceFields) ++EnabledCount; else ++DisabledCount;
    if (Doc.Domain.WorldGridEnabled) ++EnabledCount; else ++DisabledCount;

    const float StatGap = 10.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    const float StatH   = 56.0f;
    const Rectangle2D EnTile  { Bounds.X + PadX, Y, StatW, StatH };
    const Rectangle2D DisTile { EnTile.Right() + StatGap, Y, StatW, StatH };

    Surface.FillRectangle(EnTile, C.CardBg, 14.0f);
    Surface.StrokeRectangle(EnTile, C.Hairline, 14.0f, 1.0f);
    RasterizeCheckBadgeGlyph(Surface, { EnTile.X + 20.0f, EnTile.Y + 19.0f }, 9.5f, C.LiveGreen, { 12u, 18u, 14u, 255u });
    Surface.Text({ EnTile.X + 12.0f, EnTile.Bottom() - 18.0f }, 9.8f, C.TextMuted, "Enabled");
    char EnBuf[16] {};
    std::snprintf(EnBuf, sizeof(EnBuf), "%u", EnabledCount);
    Surface.TextRightAligned({ EnTile.X, EnTile.Y + 13.0f, EnTile.Width - 14.0f, 30.0f }, 25.0f, C.TextPrimary, EnBuf);

    Surface.FillRectangle(DisTile, C.CardBg, 14.0f);
    Surface.StrokeRectangle(DisTile, C.Hairline, 14.0f, 1.0f);
    Surface.FillCircle({ DisTile.X + 20.0f, DisTile.Y + 19.0f }, 9.5f, { 38u, 38u, 40u, 255u });
    RasterizeEyeGlyph(Surface, { DisTile.X + 20.0f, DisTile.Y + 19.0f }, 4.6f, C.TextMuted, true);
    Surface.Text({ DisTile.X + 12.0f, DisTile.Bottom() - 18.0f }, 9.8f, C.TextMuted, "Disabled");
    char DisBuf[16] {};
    std::snprintf(DisBuf, sizeof(DisBuf), "%u", DisabledCount);
    Surface.TextRightAligned({ DisTile.X, DisTile.Y + 13.0f, DisTile.Width - 14.0f, 30.0f }, 25.0f, C.TextPrimary, DisBuf);

    Y += StatH + 10.0f;

    // 3. Recessed Search Pill ("Search scene..." + Ctrl ⇧ F)
    const Rectangle2D SearchRect { Bounds.X + PadX, Y, InnerW, 30.0f };
    Surface.FillRectangle(SearchRect, C.WellBg, 15.0f);
    Surface.StrokeRectangle(SearchRect, C.Hairline, 15.0f, 1.0f);
    Surface.StrokeCircle({ SearchRect.X + 15.0f, SearchRect.Center().Y - 1.0f }, 4.5f, C.TextMuted, 1.1f);
    Surface.Text({ SearchRect.X + 27.0f, SearchRect.Center().Y - 5.5f }, 10.2f, C.TextMuted, "Search scene...");
    Surface.TextRightAligned({ SearchRect.X, SearchRect.Y + 8.0f, SearchRect.Width - 12.0f, 14.0f }, 8.6f, { 95u, 95u, 100u, 255u }, "Ctrl F");
    Y += 38.0f;

    // 4. Filter Chips: All | Gas | Geometry | Lights
    struct FilterChip { FluidOutlinerFilter Id; const char* Label; float W; };
    constexpr std::array<FilterChip, 4> Filters {{
        { FluidOutlinerFilter::All,      "All",      34.0f },
        { FluidOutlinerFilter::Gas,      "Gas",      40.0f },
        { FluidOutlinerFilter::Geometry, "Geometry", 66.0f },
        { FluidOutlinerFilter::Lights,   "Lights",   50.0f },
    }};
    float FX = Bounds.X + PadX;
    for (const auto& F : Filters)
    {
        const Rectangle2D Chip { FX, Y, F.W, 22.0f };
        const bool Active = (Session.OutlinerFilter == F.Id);
        const bool Hov    = ControlAppearance::QueryPointerOver(Chip, Pointer);
        Surface.FillRectangle(Chip, Active ? ColorRgba { 36u, 33u, 28u, 255u } : (Hov ? C.CardElevated : C.CardBg), 11.0f);
        Surface.StrokeRectangle(Chip, Active ? C.WarmAccent : C.Hairline, 11.0f, 0.9f);
        Surface.TextCentered(Chip, 9.5f, Active ? C.WarmAccent : C.TextMuted, F.Label);
        if (Hov && Pointer.Pressed)
        {
            Session.OutlinerFilter = F.Id;
        }
        FX += F.W + 5.0f;
    }
    Y += 30.0f;

    // 5. Scene Collection Header + "+" Add button
    Surface.Text({ Bounds.X + PadX, Y + 4.0f }, 10.5f, C.TextSoft, "v  Scene collection");
    const Rectangle2D AddNodeBtn { Bounds.Right() - PadX - 22.0f, Y, 22.0f, 22.0f };
    const bool AddNodeHov = ControlAppearance::QueryPointerOver(AddNodeBtn, Pointer);
    Surface.FillCircle(AddNodeBtn.Center(), 11.0f, AddNodeHov ? C.CardElevated : C.CardBg);
    RasterizePlusGlyph(Surface, AddNodeBtn.Center(), 4.0f, C.TextSoft);
    if (AddNodeHov && Pointer.Pressed)
    {
        Session.PresetBrowserOpen = true;
    }
    Y += 28.0f;

    // 6. Scene Tree Rows
    struct NodeEntry
    {
        FluidOutlinerNode   Id;
        const char*         Label;
        const char*         Tag;
        FluidOutlinerFilter Group;
        uint8_t             IconKind; // 0=cube, 1=flame, 2=sun, 3=wind
    };
    constexpr std::array<NodeEntry, 7> Nodes {{
        { FluidOutlinerNode::HeroVolume,     "Gas domain",       "VOLUME", FluidOutlinerFilter::Gas,      0u },
        { FluidOutlinerNode::EmitterSource,  "Fire emitter",     "SOURCE", FluidOutlinerFilter::Gas,      1u },
        { FluidOutlinerNode::GiLighting,     "Directional light","LIGHT",  FluidOutlinerFilter::Lights,   2u },
        { FluidOutlinerNode::SceneObstacle,  "Scene collider",   "SDF",    FluidOutlinerFilter::Geometry, 0u },
        { FluidOutlinerNode::ForceFields,    "ForceFields wind", "WIND",   FluidOutlinerFilter::Gas,      3u },
        { FluidOutlinerNode::WorldDriftGrid, "World drift grid", "DRIFT",  FluidOutlinerFilter::Gas,      0u },
        { FluidOutlinerNode::ParticleEmbers, "Embers & GI",      "FX",     FluidOutlinerFilter::Lights,   1u },
    }};

    const std::size_t MaxShownNodes = Session.CompactOutliner ? 3u : 6u;
    std::size_t ShownCount = 0u;
    for (const auto& N : Nodes)
    {
        if (ShownCount >= MaxShownNodes) break;
        if (Session.OutlinerFilter != FluidOutlinerFilter::All && N.Group != Session.OutlinerFilter) continue;

        const Rectangle2D RowRect { Bounds.X + PadX, Y, InnerW, 33.0f };
        LastNodeRowRects[static_cast<std::size_t>(N.Id)] = RowRect;
        const bool Selected = (Session.SelectedNode == N.Id);
        const bool Hovered  = ControlAppearance::QueryPointerOver(RowRect, Pointer);
        if (Hovered && Pointer.Pressed)
        {
            Session.SelectedNode = N.Id;
        }

        if (Selected || Hovered)
        {
            Surface.FillRectangle(RowRect, Selected ? ColorRgba { 32u, 29u, 26u, 255u } : C.CardBg, 10.0f);
        }
        if (Selected)
        {
            Surface.StrokeRectangle(RowRect, C.WarmAccent, 10.0f, 1.0f);
            Surface.FillRectangle({ RowRect.X + 2.0f, RowRect.Y + 6.0f, 2.5f, RowRect.Height - 12.0f }, C.WarmAccent, 1.2f);
        }

        const Point2D IconC { RowRect.X + 20.0f, RowRect.Center().Y };
        const ColorRgba IconTint = (N.IconKind == 1u) ? C.WarmAccent
                                 : (N.IconKind == 2u) ? ColorRgba { 240u, 198u, 96u, 255u }
                                 : (N.IconKind == 3u) ? C.TealTint
                                                      : ColorRgba { 98u, 174u, 240u, 255u };
        if (N.IconKind == 1u)      DrawFlameGlyph(Surface, IconC, 5.5f, IconTint);
        else if (N.IconKind == 2u) DrawSunGlyph(Surface, IconC, 5.8f, IconTint);
        else if (N.IconKind == 3u) RasterizeWindLinesGlyph(Surface, IconC, 5.0f, IconTint);
        else                       DrawCubeGlyph(Surface, IconC, 5.5f, IconTint);

        Surface.Text({ RowRect.X + 35.0f, RowRect.Center().Y - 6.0f }, 11.2f, Selected ? C.TextPrimary : C.TextSoft, N.Label);
        if (Selected)
        {
            RasterizeEyeGlyph(Surface, { RowRect.Right() - 16.0f, RowRect.Center().Y }, 4.6f, C.TextMuted, false);
        }
        else
        {
            Surface.TextRightAligned({ RowRect.X, RowRect.Y + 10.0f, RowRect.Width - 10.0f, 14.0f }, 8.6f, C.TextMuted, N.Tag);
        }

        Y += 35.0f;
        ++ShownCount;
    }

    Y += 4.0f;
    Surface.Text({ Bounds.X + PadX + 4.0f, Y }, 9.4f, C.TextMuted, "Editing live simulation");
    Y += 18.0f;
    Surface.FillRectangle({ Bounds.X + PadX, Y, InnerW, 1.0f }, C.Hairline, 0.0f);
    Y += 12.0f;

    // 7. Preset Library Section (matching bottom half of HTML Left Panel)
    const auto& Presets = SimulationSpace::FluidSpace::FluidPresets::ListPresets();
    Surface.Text({ Bounds.X + PadX, Y + 1.0f }, 14.0f, C.TextPrimary, "Preset library");
    char CountBuf[16] {};
    std::snprintf(CountBuf, sizeof(CountBuf), "%zu", Presets.size());
    const Rectangle2D CountPill { Bounds.X + PadX + 98.0f, Y + 2.0f, 24.0f, 16.0f };
    Surface.FillRectangle(CountPill, { 30u, 30u, 34u, 255u }, 8.0f);
    Surface.TextCentered(CountPill, 8.8f, C.TextMuted, CountBuf);

    const Rectangle2D GalleryBtn { Bounds.Right() - PadX - 24.0f, Y, 24.0f, 22.0f };
    const bool GalHov = ControlAppearance::QueryPointerOver(GalleryBtn, Pointer);
    Surface.FillRectangle(GalleryBtn, GalHov ? C.CardElevated : C.CardBg, 6.0f);
    Surface.StrokeRectangle(GalleryBtn, C.Hairline, 6.0f, 0.8f);
    Surface.TextCentered(GalleryBtn, 9.5f, C.TextSoft, "::");
    if (GalHov && Pointer.Pressed)
    {
        Session.PresetBrowserOpen = true;
    }
    Y += 28.0f;

    // Preset search pill
    const Rectangle2D PSearch { Bounds.X + PadX, Y, InnerW, 28.0f };
    Surface.FillRectangle(PSearch, C.WellBg, 14.0f);
    Surface.StrokeRectangle(PSearch, C.Hairline, 14.0f, 1.0f);
    Surface.StrokeCircle({ PSearch.X + 14.0f, PSearch.Center().Y - 1.0f }, 4.2f, C.TextMuted, 1.0f);
    Surface.Text({ PSearch.X + 25.0f, PSearch.Center().Y - 5.0f }, 9.8f, C.TextMuted, "Find an effect...");
    Surface.TextRightAligned({ PSearch.X, PSearch.Y + 7.0f, PSearch.Width - 12.0f, 14.0f }, 9.0f, { 95u, 95u, 100u, 255u }, "/");
    Y += 34.0f;

    // Preset filter chips: All | Fire | Smoke | Blast
    struct PFilter { FluidPresetFilter Id; const char* Label; float W; };
    constexpr std::array<PFilter, 4> PFilters {{
        { FluidPresetFilter::All,   "All",   32.0f },
        { FluidPresetFilter::Fire,  "Fire",  38.0f },
        { FluidPresetFilter::Smoke, "Smoke", 48.0f },
        { FluidPresetFilter::Blast, "Blast", 42.0f },
    }};
    float PFX = Bounds.X + PadX;
    for (const auto& PF : PFilters)
    {
        const Rectangle2D Chip { PFX, Y, PF.W, 20.0f };
        const bool Active = (Session.PresetFilter == PF.Id);
        const bool Hov    = ControlAppearance::QueryPointerOver(Chip, Pointer);
        Surface.FillRectangle(Chip, Active ? ColorRgba { 34u, 34u, 38u, 255u } : (Hov ? C.CardElevated : ColorRgba { 0u, 0u, 0u, 0u }), 10.0f);
        Surface.TextCentered(Chip, 9.2f, Active ? C.TextPrimary : C.TextMuted, PF.Label);
        if (Hov && Pointer.Pressed)
        {
            Session.PresetFilter = PF.Id;
        }
        PFX += PF.W + 4.0f;
    }
    Y += 26.0f;

    // Preset cards filling remaining vertical space down to footer
    const float FooterY = Bounds.Bottom() - 28.0f;
    Surface.PushClip({ Bounds.X + PadX, Y, InnerW, std::max(0.0f, FooterY - Y - 4.0f) });
    std::size_t CardSlot = 0u;
    for (std::size_t I = 0u; I < Presets.size() && Y + 48.0f <= FooterY; ++I)
    {
        if (!MatchesPresetCategory(Presets[I].Category, Session.PresetFilter)) continue;

        const Rectangle2D Card { Bounds.X + PadX, Y, InnerW, 48.0f };
        if (CardSlot < LastPresetCardRects.size())
        {
            LastPresetCardRects[CardSlot] = Card;
        }
        const bool Active = (Session.ActivePresetId == Presets[I].Key);
        const bool Hov    = ControlAppearance::QueryPointerOver(Card, Pointer);
        Surface.FillRectangle(Card, Active ? ColorRgba { 32u, 29u, 26u, 255u } : (Hov ? C.CardElevated : C.CardBg), 12.0f);
        Surface.StrokeRectangle(Card, Active ? C.WarmAccent : C.Hairline, 12.0f, 0.9f);

        const Rectangle2D IconBox { Card.X + 9.0f, Card.Y + 9.0f, 30.0f, 30.0f };
        Surface.FillCircle(IconBox.Center(), 14.0f, { 28u, 28u, 32u, 255u });
        DrawFlameGlyph(Surface, IconBox.Center(), 5.8f, Active ? C.WarmAccent : C.TextSoft);

        Surface.PushClip({ IconBox.Right() + 8.0f, Card.Y + 4.0f, Card.Width - 70.0f, 40.0f });
        Surface.Text({ IconBox.Right() + 8.0f, Card.Y + 8.0f }, 10.8f, C.TextPrimary, Presets[I].Title.data());
        Surface.Text({ IconBox.Right() + 8.0f, Card.Y + 25.0f }, 8.8f, C.TextMuted, Presets[I].Summary.data());
        Surface.PopClip();
        Surface.TextRightAligned({ Card.X, Card.Y + 14.0f, Card.Width - 12.0f, 16.0f }, 10.5f, C.TextMuted, "->");

        if (Hov && Pointer.Pressed)
        {
            Session.LoadPreset(Presets[I].Key);
        }
        Y += 54.0f;
        ++CardSlot;
    }
    Surface.PopClip();

    // Left panel footer
    Surface.FillRectangle({ Bounds.X + PadX, FooterY - 2.0f, InnerW, 1.0f }, C.Hairline, 0.0f);
    Surface.Text({ Bounds.X + PadX + 4.0f, FooterY + 6.0f }, 9.2f, C.TextMuted, "One domain · one source · one collider");
}

} // namespace Frontier::Workspaces
