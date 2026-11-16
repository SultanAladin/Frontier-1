/*============================================================================================================================================
                                                         FRACTUREOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 FractureOutlinerPanel implementation: 1:1 HTML Outliner layout with Released/Held census cards, search, filter chips & hierarchy

#include "Engine/Workspaces/Fracture/FractureOutlinerPanel.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace ParticleAppearance;
using GeometrySpace::Fracture::FractureExecutionMode;
using GeometrySpace::Fracture::FractureSolidCategory;

struct SolidEntryInfo
{
    FractureSolidCategory Category;
    const char*           Title;
    const char*           Dimensions;
};

static constexpr std::array<SolidEntryInfo, 8> SolidPrimitives {{
    { FractureSolidCategory::Monolith,    "Monolith",     "0.85 x 0.85 x 1.85 m · concrete" },
    { FractureSolidCategory::CurtainPane, "Curtain Pane", "1.70 x 1.15 m · 14 mm glass" },
    { FractureSolidCategory::TimberBeam,  "Timber Beam",  "2.40 x 0.28 x 0.28 m · joist" },
    { FractureSolidCategory::Cylinder,    "Cylinder",     "D 0.80 x 1.20 m · column" },
    { FractureSolidCategory::Cone,        "Cone",         "D 1.00 x 1.10 m · spire" },
    { FractureSolidCategory::Boulder,      "Boulder",      "1.25 m · faceted granite" },
    { FractureSolidCategory::HollowCavity, "Hollow Cavity","1.20 x 0.60 x 0.60 m · core" },
    { FractureSolidCategory::Cube,         "Cube",         "1.00 x 1.00 x 1.00 m · block" },
}};

[[nodiscard]] const char* QuerySolverShortName(GeometrySpace::Fracture::FractureSolverMethod Method) noexcept
{
    using GeometrySpace::Fracture::FractureSolverMethod;
    switch (Method)
    {
    case FractureSolverMethod::FastRuntimeBoussinesq: return "Boussinesq Fast";
    case FractureSolverMethod::EngineeringBakedSolid: return "Navier-Cauchy";
    case FractureSolverMethod::GriffithStressBudget:  return "Griffith Energy";
    case FractureSolverMethod::LefmWavefront:         return "LEFM Wavefront";
    case FractureSolverMethod::ClusteredVoronoi:      return "Clustered Voronoi";
    case FractureSolverMethod::DendriticSpallHull:    return "Dendritic Spall";
    }
    return "Fracture";
}

[[nodiscard]] bool ContainsAsciiInsensitive(const std::string& Haystack, const std::string& Needle) noexcept
{
    if (Needle.empty()) return true;
    auto It = std::search(
        Haystack.begin(), Haystack.end(),
        Needle.begin(), Needle.end(),
        [](char A, char B) noexcept
        {
            return std::tolower(static_cast<unsigned char>(A)) == std::tolower(static_cast<unsigned char>(B));
        });
    return It != Haystack.end();
}

void DrawSolidIconGlyph(PixelSpace& Surface, Rectangle2D Box, FractureSolidCategory Cat, ColorRgba Ink) noexcept
{
    const Point2D C = Box.Center();
    Surface.FillRectangle(Box, ColorRgba { 68u, 134u, 224u, 28u }, 7.0f);
    Surface.StrokeRectangle(Box, ColorRgba { 96u, 165u, 250u, 72u }, 7.0f, 0.8f);
    if (Cat == FractureSolidCategory::Cylinder || Cat == FractureSolidCategory::Boulder)
    {
        Surface.StrokeCircle(C, 5.2f, Ink, 1.2f);
        Surface.StrokeLine({ C.X - 5.0f, C.Y }, { C.X + 5.0f, C.Y }, Ink.ScaledAlpha(0.65f), 0.9f);
    }
    else if (Cat == FractureSolidCategory::Cone)
    {
        Surface.StrokeLine({ C.X, C.Y - 5.5f }, { C.X - 5.0f, C.Y + 4.5f }, Ink, 1.2f);
        Surface.StrokeLine({ C.X, C.Y - 5.5f }, { C.X + 5.0f, C.Y + 4.5f }, Ink, 1.2f);
        Surface.StrokeLine({ C.X - 5.0f, C.Y + 4.5f }, { C.X + 5.0f, C.Y + 4.5f }, Ink, 1.2f);
    }
    else
    {
        const Rectangle2D Inner { C.X - 4.8f, C.Y - 4.8f, 9.6f, 9.6f };
        Surface.StrokeRectangle(Inner, Ink, 1.5f, 1.15f);
        Surface.StrokeLine({ Inner.X, Inner.Y + 3.5f }, { Inner.Right(), Inner.Y + 3.5f }, Ink.ScaledAlpha(0.55f), 0.9f);
    }
}

} // namespace

void FractureOutlinerPanel::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    FractureWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     WheelDeltaY) noexcept
{
    if (Bounds.Width < 160.0f || Bounds.Height < 220.0f)
    {
        return;
    }

    const auto C = QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg, 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 22.0f, 1.0f);

    const float PadX   = 16.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 18.0f;

    // 1. Header: "Outliner" + subtitle + Compact & Spawn circle buttons
    Surface.Text({ Bounds.X + PadX, Y }, 19.0f, C.TextPrimary, "Outliner");
    char SubBuf[64];
    std::snprintf(SubBuf, sizeof(SubBuf), "Fracture directory · %zu fragments",
        Session.PartitionResult.Fragments.size());
    Surface.Text({ Bounds.X + PadX, Y + 25.0f }, 9.8f, C.TextMuted, SubBuf);

    const Rectangle2D AddBtn     { Bounds.Right() - PadX - 28.0f, Y + 3.0f, 28.0f, 28.0f };
    const Rectangle2D CompactBtn { AddBtn.X - 34.0f,              Y + 3.0f, 28.0f, 28.0f };
    const bool CompactHover = ControlAppearance::QueryPointerOver(CompactBtn, Pointer);
    const bool AddHover     = ControlAppearance::QueryPointerOver(AddBtn, Pointer);

    Surface.FillCircle(CompactBtn.Center(), 14.0f, CompactHover ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(CompactBtn.Center(), 14.0f, Session.CompactOutliner ? C.WarmAccent : C.Hairline, 1.0f);
    Surface.StrokeLine({ CompactBtn.Center().X - 2.6f, CompactBtn.Center().Y - 4.2f },
                       { CompactBtn.Center().X - 2.6f, CompactBtn.Center().Y + 4.2f }, C.TextSoft, 1.3f);
    Surface.StrokeLine({ CompactBtn.Center().X + 2.6f, CompactBtn.Center().Y - 4.2f },
                       { CompactBtn.Center().X + 2.6f, CompactBtn.Center().Y + 4.2f }, C.TextSoft, 1.3f);
    if (CompactHover && Pointer.Pressed)
    {
        Session.CompactOutliner = !Session.CompactOutliner;
    }

    Surface.FillCircle(AddBtn.Center(), 14.0f, AddHover ? C.CardElevated : C.CardBg);
    Surface.StrokeCircle(AddBtn.Center(), 14.0f, AddHover ? C.WarmAccent : C.Hairline, 1.0f);
    RasterizePlusGlyph(Surface, AddBtn.Center(), 4.5f, AddHover ? C.WarmAccent : C.TextPrimary);
    if (AddHover && Pointer.Pressed)
    {
        Session.ApplyPreset((Session.ActivePresetIndex + 1u) % FractureWorkspaceSession::PresetCount);
    }

    Y += 46.0f;

    // 2. Released / Held Census Stat Cards (unless compacted)
    if (!Session.CompactOutliner)
    {
        const float StatGap = 10.0f;
        const float StatW   = (InnerW - StatGap) * 0.5f;
        const float StatH   = 62.0f;
        const Rectangle2D RelTile  { Bounds.X + PadX, Y, StatW, StatH };
        const Rectangle2D HeldTile { RelTile.Right() + StatGap, Y, StatW, StatH };

        Surface.FillRectangle(RelTile, C.CardBg, 14.0f);
        Surface.StrokeRectangle(RelTile, C.Hairline, 14.0f, 1.0f);
        RasterizeCheckBadgeGlyph(Surface, { RelTile.X + 21.0f, RelTile.Y + 21.0f }, 10.0f, C.LiveGreen, { 12u, 18u, 14u, 255u });
        Surface.Text({ RelTile.X + 12.0f, RelTile.Bottom() - 20.0f }, 10.0f, C.TextMuted, "Released");
        char RelBuf[16];
        std::snprintf(RelBuf, sizeof(RelBuf), "%u", Session.CountReleasedFragments());
        Surface.TextRightAligned({ RelTile.X, RelTile.Y + 16.0f, RelTile.Width - 14.0f, 34.0f }, 27.0f, C.TextPrimary, RelBuf);

        Surface.FillRectangle(HeldTile, C.CardBg, 14.0f);
        Surface.StrokeRectangle(HeldTile, C.Hairline, 14.0f, 1.0f);
        Surface.FillCircle({ HeldTile.X + 21.0f, HeldTile.Y + 21.0f }, 10.0f, ColorRgba { 38u, 38u, 40u, 255u });
        Surface.StrokeLine({ HeldTile.X + 21.0f, HeldTile.Y + 16.5f }, { HeldTile.X + 16.8f, HeldTile.Y + 24.5f }, C.TextMuted, 1.2f);
        Surface.StrokeLine({ HeldTile.X + 21.0f, HeldTile.Y + 16.5f }, { HeldTile.X + 25.2f, HeldTile.Y + 24.5f }, C.TextMuted, 1.2f);
        Surface.StrokeLine({ HeldTile.X + 16.8f, HeldTile.Y + 24.5f }, { HeldTile.X + 25.2f, HeldTile.Y + 24.5f }, C.TextMuted, 1.2f);
        Surface.Text({ HeldTile.X + 12.0f, HeldTile.Bottom() - 20.0f }, 10.0f, C.TextMuted, "Held");
        char HeldBuf[16];
        std::snprintf(HeldBuf, sizeof(HeldBuf), "%u", Session.CountHeldFragments());
        Surface.TextRightAligned({ HeldTile.X, HeldTile.Y + 16.0f, HeldTile.Width - 14.0f, 34.0f }, 27.0f, C.TextPrimary, HeldBuf);

        Y += StatH + 12.0f;
    }

    // 3. Recessed Search Pill ("Search directory..." + Ctrl F)
    const Rectangle2D SearchRect { Bounds.X + PadX, Y, InnerW, 32.0f };
    Surface.FillRectangle(SearchRect, C.WellBg, 16.0f);
    Surface.StrokeRectangle(SearchRect, Session.SearchFieldFocused ? C.WarmAccent : C.Hairline, 16.0f, 1.0f);
    RasterizeMagnifierGlyph(Surface, { SearchRect.X + 17.0f, SearchRect.Center().Y }, 5.0f, C.TextMuted);
    Surface.Text({ SearchRect.X + 30.0f, SearchRect.Center().Y - 5.5f }, 10.2f,
        Session.SearchFilterText.empty() ? C.TextMuted : C.TextPrimary,
        Session.SearchFilterText.empty() ? "Search directory..." : Session.SearchFilterText.c_str());
    Surface.TextRightAligned({ SearchRect.X, SearchRect.Center().Y - 5.0f, SearchRect.Width - 12.0f, 12.0f }, 8.6f, C.TextFaint, "Ctrl F");
    if (ControlAppearance::QueryPointerOver(SearchRect, Pointer) && Pointer.Pressed)
    {
        Session.SearchFieldFocused = !Session.SearchFieldFocused;
    }
    Y += 42.0f;

    // 4. Outliner Category Filter Chips
    static constexpr std::array<std::pair<FractureOutlinerFilter, const char*>, 5> FilterItems {{
        { FractureOutlinerFilter::All,        "All" },
        { FractureOutlinerFilter::Primitives, "Solids" },
        { FractureOutlinerFilter::Fragments,  "Shards" },
        { FractureOutlinerFilter::Released,   "Released" },
        { FractureOutlinerFilter::Held,       "Held" },
    }};

    float ChipX = Bounds.X + PadX;
    constexpr float ChipH = 22.0f;
    for (const auto& [FilterVal, Label] : FilterItems)
    {
        const float ChipW = Surface.MeasureTextWidth(9.2f, Label) + 16.0f;
        const Rectangle2D ChipRect { ChipX, Y, ChipW, ChipH };
        const bool Active = (Session.OutlinerFilter == FilterVal);
        const bool Hover  = ControlAppearance::QueryPointerOver(ChipRect, Pointer);
        Surface.FillRectangle(ChipRect, Active ? C.CardElevated : (Hover ? ColorRgba { 30u, 30u, 32u, 255u } : C.PanelBg), 11.0f);
        Surface.StrokeRectangle(ChipRect, Active ? C.HairlineStrong : C.Hairline, 11.0f, 0.9f);
        Surface.TextCentered(ChipRect, 9.1f, Active ? C.TextPrimary : C.TextMuted, Label);
        if (Hover && Pointer.Pressed)
        {
            Session.OutlinerFilter = FilterVal;
        }
        ChipX = ChipRect.Right() + 5.0f;
    }
    Y += ChipH + 12.0f;

    // 5. Scrollable Hierarchical Directory (Primitives, Recipe, Fragments)
    const float FooterH = 30.0f;
    const Rectangle2D ScrollRect { Bounds.X + 8.0f, Y, Bounds.Width - 16.0f, std::max(60.0f, Bounds.Bottom() - FooterH - Y) };
    if (ControlAppearance::QueryPointerOver(ScrollRect, Pointer) && std::abs(WheelDeltaY) > 0.001f)
    {
        Session.OutlinerScrollY = std::max(0.0f, Session.OutlinerScrollY - WheelDeltaY * 26.0f);
    }

    Surface.PushClip(ScrollRect);

    float RowY = ScrollRect.Y - Session.OutlinerScrollY;
    const float RowX = Bounds.X + PadX;
    const float RowW = InnerW;

    const bool ShowPrimitives = (Session.OutlinerFilter == FractureOutlinerFilter::All ||
                                 Session.OutlinerFilter == FractureOutlinerFilter::Primitives);
    const bool ShowFragments  = (Session.OutlinerFilter != FractureOutlinerFilter::Primitives);

    if (ShowPrimitives)
    {
        Surface.Text({ RowX, RowY + 2.0f }, 8.8f, C.TextMuted, "v   PRIMITIVES");
        Surface.TextRightAligned({ RowX, RowY + 2.0f, RowW, 12.0f }, 8.8f, C.TextFaint, "8");
        RowY += 22.0f;

        for (const auto& Entry : SolidPrimitives)
        {
            if (!ContainsAsciiInsensitive(Entry.Title, Session.SearchFilterText))
            {
                continue;
            }
            const Rectangle2D RowRect { RowX - 4.0f, RowY, RowW + 8.0f, 34.0f };
            const bool Sel = (Session.SolidCategory == Entry.Category);
            const bool Hov = ControlAppearance::QueryPointerOver(RowRect, Pointer);
            if (Sel || Hov)
            {
                Surface.FillRectangle(RowRect, Sel ? C.CardElevated : ColorRgba { 24u, 24u, 26u, 255u }, 10.0f);
                if (Sel)
                {
                    Surface.StrokeRectangle(RowRect, C.HairlineStrong, 10.0f, 0.8f);
                    Surface.FillRectangle({ RowRect.X + 2.0f, RowRect.Y + 7.0f, 2.5f, RowRect.Height - 14.0f }, C.WarmAccent, 1.2f);
                }
            }

            const Rectangle2D IconBox { RowX + 6.0f, RowY + 5.0f, 24.0f, 24.0f };
            DrawSolidIconGlyph(Surface, IconBox, Entry.Category, ColorRgba { 112u, 178u, 255u, 255u });

            Surface.Text({ IconBox.Right() + 9.0f, RowY + 4.0f }, 10.8f, Sel ? C.TextPrimary : C.TextSoft, Entry.Title);
            Surface.Text({ IconBox.Right() + 9.0f, RowY + 18.0f }, 8.4f, C.TextFaint, Entry.Dimensions);

            if (Sel)
            {
                RasterizeCheckBadgeGlyph(Surface, { RowX + RowW - 10.0f, RowY + 17.0f }, 7.0f, C.LiveGreen, { 12u, 18u, 14u, 255u });
            }

            if (Hov && Pointer.Pressed)
            {
                Session.SolidCategory = Entry.Category;
                Session.RebuildFracturePartition();
            }
            RowY += 36.0f;
        }

        // Recipe node
        RowY += 4.0f;
        Surface.Text({ RowX, RowY + 2.0f }, 8.8f, C.TextMuted, "v   RECIPE");
        RowY += 20.0f;

        const Rectangle2D RecipeRow { RowX - 4.0f, RowY, RowW + 8.0f, 34.0f };
        const bool RecipeHov = ControlAppearance::QueryPointerOver(RecipeRow, Pointer);
        if (RecipeHov)
        {
            Surface.FillRectangle(RecipeRow, ColorRgba { 24u, 24u, 26u, 255u }, 10.0f);
        }
        const auto& Mat = Session.ActiveMaterialSpec();
        auto ToByte = [](float Linear) noexcept -> uint8_t
        {
            const float Srgb = std::pow(std::clamp(Linear, 0.0f, 1.0f), 1.0f / 2.2f);
            return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
        };
        const ColorRgba MatTint {
            ToByte(Mat.ExteriorLinearRgb.X),
            ToByte(Mat.ExteriorLinearRgb.Y),
            ToByte(Mat.ExteriorLinearRgb.Z),
            255u
        };
        Surface.FillCircle({ RowX + 18.0f, RowY + 17.0f }, 9.0f, MatTint);
        Surface.StrokeCircle({ RowX + 18.0f, RowY + 17.0f }, 9.0f, ColorRgba { 255u, 255u, 255u, 48u }, 0.8f);
        char RecipeLine[64];
        std::snprintf(RecipeLine, sizeof(RecipeLine), "%s · %.2f kJ", Mat.Label, Session.ImpactEnergyJ / 1000.0);
        Surface.Text({ RowX + 35.0f, RowY + 5.0f }, 10.4f, C.TextPrimary, RecipeLine);

        const char* ModeCaption = (Session.ExecutionMode == FractureExecutionMode::Realtime) ? "Realtime"
                                : (Session.ExecutionMode == FractureExecutionMode::Baked)    ? "Baked"
                                                                                             : "Bonds";
        Surface.Text({ RowX + 35.0f, RowY + 19.0f }, 8.5f, C.TextFaint, QuerySolverShortName(Session.SolverMethod));
        const Rectangle2D ModeBadge { RowX + RowW - 50.0f, RowY + 8.0f, 46.0f, 18.0f };
        Surface.FillRectangle(ModeBadge, ColorRgba { 62u, 196u, 109u, 28u }, 9.0f);
        Surface.TextCentered(ModeBadge, 8.2f, C.LiveGreen, ModeCaption);
        if (RecipeHov && Pointer.Pressed)
        {
            Session.ActiveInspectorTab = 1u;
        }
        RowY += 42.0f;
    }

    // Fragments stream
    if (ShowFragments)
    {
        const auto& Frags = Session.PartitionResult.Fragments;
        Surface.Text({ RowX, RowY + 2.0f }, 8.8f, C.TextMuted, "v   FRAGMENTS");
        char FragHeader[32];
        std::snprintf(FragHeader, sizeof(FragHeader), "%zu pcs · %u rel", Frags.size(), Session.CountReleasedFragments());
        Surface.TextRightAligned({ RowX, RowY + 2.0f, RowW, 12.0f }, 8.6f, C.TextFaint, FragHeader);
        RowY += 22.0f;

        for (size_t I = 0u; I < Frags.size(); ++I)
        {
            const auto& F = Frags[I];
            const bool IsReleased = Session.QueryFragmentReleased(I);
            if (Session.OutlinerFilter == FractureOutlinerFilter::Released && !IsReleased) continue;
            if (Session.OutlinerFilter == FractureOutlinerFilter::Held && IsReleased) continue;

            char LabelBuf[32];
            std::snprintf(LabelBuf, sizeof(LabelBuf), "Fragment %02u", F.Index + 1u);
            if (!ContainsAsciiInsensitive(LabelBuf, Session.SearchFilterText) &&
                !ContainsAsciiInsensitive(F.Token, Session.SearchFilterText))
            {
                continue;
            }

            if (RowY + 32.0f < ScrollRect.Y)
            {
                RowY += 34.0f;
                continue;
            }
            if (RowY > ScrollRect.Bottom())
            {
                break;
            }

            const Rectangle2D RowRect { RowX - 4.0f, RowY, RowW + 8.0f, 32.0f };
            const bool Sel = (Session.RenderOptions.SelectedFragmentIndex == static_cast<int32_t>(F.Index));
            const bool Hov = ControlAppearance::QueryPointerOver(RowRect, Pointer);
            const bool Vis = (I < Session.FragmentVisible.size()) ? Session.FragmentVisible[I] : true;

            if (Sel || Hov)
            {
                Surface.FillRectangle(RowRect, Sel ? C.CardElevated : ColorRgba { 24u, 24u, 26u, 255u }, 10.0f);
                if (Sel)
                {
                    Surface.StrokeRectangle(RowRect, C.WarmAccent.ScaledAlpha(0.55f), 10.0f, 0.8f);
                }
            }

            uint8_t SwR = 180u, SwG = 140u, SwB = 80u;
            if (Session.RenderOptions.RenderMode == FractureRenderViewMode::Cells)
            {
                FractureViewportRenderer::SampleCellSwatchSrgb(F.Index, SwR, SwG, SwB);
            }
            else
            {
                FractureViewportRenderer::SampleEnergySwatchSrgb(F.StressRatio, SwR, SwG, SwB);
            }
            Surface.FillRectangle({ RowX + 6.0f, RowY + 10.0f, 11.0f, 11.0f }, { SwR, SwG, SwB, Vis ? uint8_t(255u) : uint8_t(90u) }, 3.5f);

            Surface.Text({ RowX + 24.0f, RowY + 4.0f }, 10.2f, Vis ? (Sel ? C.TextPrimary : C.TextSoft) : C.TextFaint, LabelBuf);
            char MetaBuf[48];
            std::snprintf(MetaBuf, sizeof(MetaBuf), "%.1f kg · %u cuts", F.MassKg, static_cast<unsigned>(F.Polyhedron.Faces.size()));
            Surface.Text({ RowX + 24.0f, RowY + 17.5f }, 8.2f, C.TextFaint, MetaBuf);

            char EnergyBuf[24];
            std::snprintf(EnergyBuf, sizeof(EnergyBuf), "%.0f J", Session.QueryFragmentEnergyJ(I));
            Surface.TextRightAligned({ RowX, RowY + 9.0f, RowW - 38.0f, 14.0f }, 9.2f, IsReleased ? C.TextSoft : C.TextFaint, EnergyBuf);

            const Rectangle2D EyeHit { RowX + RowW - 34.0f, RowY + 6.0f, 18.0f, 20.0f };
            RasterizeEyeGlyph(Surface, EyeHit.Center(), 4.2f, Vis ? C.TextMuted : C.DangerRed.ScaledAlpha(0.75f), !Vis);

            const Point2D RelDot { RowX + RowW - 9.0f, RowY + 16.0f };
            if (IsReleased)
            {
                RasterizeCheckBadgeGlyph(Surface, RelDot, 6.2f, C.LiveGreen, { 12u, 18u, 14u, 255u });
            }
            else
            {
                Surface.StrokeCircle(RelDot, 5.5f, C.HairlineStrong, 1.0f);
            }

            if (ControlAppearance::QueryPointerOver(EyeHit, Pointer) && Pointer.Pressed && I < Session.FragmentVisible.size())
            {
                Session.FragmentVisible[I] = !Session.FragmentVisible[I];
                ++Session.RevisionCounter;
            }
            else if (Hov && Pointer.Pressed)
            {
                Session.RenderOptions.SelectedFragmentIndex = Sel ? -1 : static_cast<int32_t>(F.Index);
            }

            RowY += 34.0f;
        }
    }

    const float TotalContentH = (RowY + Session.OutlinerScrollY) - ScrollRect.Y;
    const float MaxScroll = std::max(0.0f, TotalContentH - ScrollRect.Height + 12.0f);
    Session.OutlinerScrollY = std::clamp(Session.OutlinerScrollY, 0.0f, MaxScroll);

    Surface.PopClip();

    // 6. Left Panel Footer
    const Rectangle2D FooterRect { Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.StrokeLine({ Bounds.X + 12.0f, FooterRect.Y }, { Bounds.Right() - 12.0f, FooterRect.Y }, C.Hairline, 1.0f);
    Surface.FillCircle({ Bounds.X + 18.0f, FooterRect.Center().Y }, 2.8f, C.LiveGreen);
    Surface.Text({ Bounds.X + 27.0f, FooterRect.Center().Y - 5.0f }, 9.0f, C.TextMuted,
        "FEM Navier-Cauchy · Boussinesq Flat-BSP");
}

} // namespace Frontier::Workspaces::Fracture
