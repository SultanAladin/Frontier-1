/*============================================================================================================================================
                                                         FRACTUREOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureOutlinerPanel matching Port 8091 HTML Fracture Editor & FrontierEditor Project Zero styling 1:1

#include "Engine/Workspaces/Fracture/FractureOutlinerPanel.h"

#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Panes/StudioPaneAppearance.h"
#include "Engine/Workspaces/Fracture/FractureIcons.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace StudioAppearance;

[[nodiscard]] bool MatchesSearch(const std::string& Text, const std::string& Query) noexcept
{
    if (Query.empty()) return true;
    auto ToLower = [](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); };
    std::string Hay = Text, Needle = Query;
    std::transform(Hay.begin(), Hay.end(), Hay.begin(), ToLower);
    std::transform(Needle.begin(), Needle.end(), Needle.begin(), ToLower);
    return Hay.find(Needle) != std::string::npos;
}

[[nodiscard]] std::string PrimitiveShortDescription(FractureIcons::PrimitiveShapeId Shape)
{
    switch (Shape)
    {
    case FractureIcons::PrimitiveShapeId::Box:       return "1.00 × 1.00 × 1.00 m";
    case FractureIcons::PrimitiveShapeId::Sphere:    return "Ø 1.00 m";
    case FractureIcons::PrimitiveShapeId::Cylinder:  return "Ø 0.80 × 1.20 m";
    case FractureIcons::PrimitiveShapeId::Cone:      return "Ø 1.00 × 1.10 m";
    case FractureIcons::PrimitiveShapeId::Capsule:   return "Ø 0.60 × 1.30 m";
    case FractureIcons::PrimitiveShapeId::Torus:     return "R 0.50 · r 0.19 m · segmented";
    case FractureIcons::PrimitiveShapeId::Pipe:      return "Ø 0.96 / 0.68 × 0.80 m · segmented";
    case FractureIcons::PrimitiveShapeId::Pane:      return "1.70 × 1.15 m · 14 mm";
    case FractureIcons::PrimitiveShapeId::HexColumn: return "0.56 AF × 1.20 m";
    default: return "";
    }
}

[[nodiscard]] ColorRgba MaterialLinearToSrgbSwatch(Mathematics::Scalar3D Lin) noexcept
{
    auto ToSrgb = [](float V) -> uint8_t
    {
        const float C = std::clamp(V, 0.0f, 1.0f);
        const float S = (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
    };
    return { ToSrgb(Lin.X), ToSrgb(Lin.Y), ToSrgb(Lin.Z), 255u };
}

} // namespace

void FractureOutlinerPanel::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    FractureWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     WheelDeltaY) noexcept
{
    if (Bounds.Width < 180.0f || Bounds.Height < 220.0f)
    {
        return;
    }

    const ColorRgba DockBg { 18u, 18u, 18u, 255u };
    Surface.FillRectangle(Bounds, DockBg, 0.0f);

    const float PadX   = 13.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 22.0f;

    // 1. Outliner Heading: "Outliner" + "Fracture · N fragments" + Collapse Sliders round button
    {
        Surface.Text({ Bounds.X + 20.0f, Y }, 20.0f, ColorRgba { 228u, 228u, 226u, 255u }, "Outliner");
        const float TitleW = Surface.MeasureTextWidth(20.0f, "Outliner");
        char ScopeStr[48] {};
        std::snprintf(ScopeStr, sizeof(ScopeStr), "Fracture · %zu fragments", Session.PartitionResult.Fragments.size());
        Surface.Text({ Bounds.X + 20.0f + TitleW + 10.0f, Y + 6.5f }, 9.6f, ColorRgba { 142u, 142u, 140u, 255u }, ScopeStr);

        const Rectangle2D CollapseBtn { Bounds.Right() - 45.0f, Y - 2.0f, 31.0f, 31.0f };
        const bool ColHov = ControlAppearance::QueryPointerOver(CollapseBtn, Pointer);
        Surface.FillCircle(CollapseBtn.Center(), 15.5f, ColHov ? ColorRgba { 36u, 36u, 38u, 255u } : ColorRgba { 27u, 27u, 27u, 255u });
        Surface.StrokeCircle(CollapseBtn.Center(), 15.5f, ColorRgba { 255u, 255u, 255u, 18u }, 1.0f);
        FractureIcons::DrawSlidersGlyph(Surface, { CollapseBtn.Center().X - 7.5f, CollapseBtn.Center().Y - 7.5f, 15.0f, 15.0f },
            ColHov ? ColorRgba { 228u, 228u, 226u, 255u } : ColorRgba { 154u, 154u, 152u, 255u });
        if (ColHov && Pointer.Pressed)
        {
            const bool AllShut = !Session.GroupExpanded[0] && !Session.GroupExpanded[1] && !Session.GroupExpanded[2];
            Session.GroupExpanded[0] = AllShut;
            Session.GroupExpanded[1] = AllShut;
            Session.GroupExpanded[2] = AllShut;
        }
    }
    Y += 38.0f;

    // 2. Two 84px Glass Census Stat Cards: Released (green check) & Held (grey triangle)
    {
        const float Gap   = 8.0f;
        const float CardW = (InnerW - Gap) * 0.5f;
        const float CardH = 84.0f;
        const uint32_t ReleasedCount = Session.CountReleasedFragments();
        const uint32_t HeldCount     = Session.CountHeldFragments();

        auto DrawCensusCard = [&](float X, const char* Label, uint32_t Count, bool IsReleased, FractureOutlinerFilter FilterTarget)
        {
            const Rectangle2D Card { X, Y, CardW, CardH };
            const bool Active = (Session.OutlinerFilter == FilterTarget);
            const bool Hov    = ControlAppearance::QueryPointerOver(Card, Pointer);
            Surface.FillRectangle(Card, Active ? ColorRgba { 30u, 30u, 31u, 255u } : ColorRgba { 23u, 23u, 24u, 255u }, 20.0f);
            Surface.StrokeRectangle(Card, Active || Hov ? ColorRgba { 255u, 255u, 255u, 42u } : ColorRgba { 255u, 255u, 255u, 20u }, 20.0f, 1.0f);

            const Point2D BadgeC { Card.X + 25.0f, Card.Y + 24.0f };
            if (IsReleased)
            {
                Surface.FillCircle(BadgeC, 11.0f, ColorRgba { 52u, 199u, 89u, 255u });
                FractureIcons::DrawCheckGlyph(Surface, { BadgeC.X - 6.5f, BadgeC.Y - 6.5f, 13.0f, 13.0f }, ColorRgba { 11u, 36u, 18u, 255u }, 2.6f);
            }
            else
            {
                Surface.FillCircle(BadgeC, 11.0f, ColorRgba { 38u, 38u, 38u, 255u });
                FractureIcons::DrawTriangleGlyph(Surface, { BadgeC.X - 6.0f, BadgeC.Y - 6.0f, 12.0f, 12.0f }, ColorRgba { 98u, 98u, 96u, 255u });
            }

            Surface.Text({ Card.X + 14.0f, Card.Bottom() - 25.0f }, 11.2f, ColorRgba { 176u, 176u, 174u, 255u }, Label);
            char NumStr[16] {};
            std::snprintf(NumStr, sizeof(NumStr), "%u", Count);
            Surface.TextRightAligned({ Card.X, Card.Bottom() - 40.0f, Card.Width - 15.0f, 32.0f }, 28.0f, ColorRgba { 238u, 238u, 236u, 255u }, NumStr);

            if (Hov && Pointer.Pressed)
            {
                Session.OutlinerFilter = Active ? FractureOutlinerFilter::All : FilterTarget;
            }
        };

        DrawCensusCard(Bounds.X + PadX, "Released", ReleasedCount, true, FractureOutlinerFilter::Released);
        DrawCensusCard(Bounds.X + PadX + CardW + Gap, "Held", HeldCount, false, FractureOutlinerFilter::Held);
    }
    Y += 93.0f;

    // 3. Tools Row: Search Pill (38px) + Filter Dropdown Pill
    static constexpr std::array<const char*, 5> FilterLabels {{
        "All", "Released", "Held", "Hidden", "Seams"
    }};
    const Rectangle2D DropRect { Bounds.X + PadX + InnerW - 76.0f, Y, 76.0f, 38.0f };
    {
        const float SearchW = InnerW - 84.0f;
        const Rectangle2D SearchRect { Bounds.X + PadX, Y, SearchW, 38.0f };
        Surface.FillRectangle(SearchRect, ColorRgba { 16u, 16u, 16u, 255u }, 19.0f);
        Surface.StrokeRectangle(SearchRect, ColorRgba { 255u, 255u, 255u, 22u }, 19.0f, 1.0f);
        FractureIcons::DrawSearchGlyph(Surface, { SearchRect.X + 13.0f, SearchRect.Center().Y - 7.0f, 14.0f, 14.0f }, ColorRgba { 138u, 138u, 136u, 255u });

        (void)TextInputField::Construct(
            Surface,
            { SearchRect.X + 30.0f, SearchRect.Y + 6.0f, SearchRect.Width - 76.0f, 26.0f },
            Session.OutlinerSearch,
            Pointer,
            10.6f,
            1.0f);
        if (Session.OutlinerSearch.Length == 0u && !Session.OutlinerSearch.Active)
        {
            Surface.Text({ SearchRect.X + 36.0f, SearchRect.Center().Y - 5.5f }, 10.4f, ColorRgba { 128u, 128u, 126u, 255u }, "Search");
        }

        const Rectangle2D KbdRect { SearchRect.Right() - 42.0f, SearchRect.Center().Y - 9.0f, 34.0f, 18.0f };
        Surface.FillRectangle(KbdRect, ColorRgba { 255u, 255u, 255u, 12u }, 5.0f);
        Surface.TextCentered(KbdRect, 8.2f, ColorRgba { 122u, 122u, 120u, 255u }, "Ctrl+F");

        const bool DropHov = ControlAppearance::QueryPointerOver(DropRect, Pointer);
        Surface.FillRectangle(DropRect, DropHov ? ColorRgba { 24u, 24u, 25u, 255u } : ColorRgba { 16u, 16u, 16u, 255u }, 19.0f);
        Surface.StrokeRectangle(DropRect, ColorRgba { 255u, 255u, 255u, 22u }, 19.0f, 1.0f);
        FractureIcons::DrawFilterGlyph(Surface, { DropRect.X + 11.0f, DropRect.Center().Y - 6.0f, 12.0f, 12.0f }, ColorRgba { 138u, 138u, 136u, 255u });
        Surface.Text({ DropRect.X + 27.0f, DropRect.Center().Y - 5.5f }, 10.2f, ColorRgba { 181u, 181u, 179u, 255u },
            FilterLabels[static_cast<uint32_t>(Session.OutlinerFilter) % 5u]);
        FractureIcons::DrawChevronDownGlyph(Surface, { DropRect.Right() - 19.0f, DropRect.Center().Y - 6.0f, 12.0f, 12.0f }, ColorRgba { 138u, 138u, 136u, 255u });

        if (DropHov && Pointer.Pressed)
        {
            Session.FilterDropdownOpen = !Session.FilterDropdownOpen;
        }
    }
    Y += 48.0f;

    // 4. Bottom 46px 4-slot Status Footer (REALTIME | BAKE | TRIS | SEED)
    constexpr float FooterH = 46.0f;
    const Rectangle2D FooterRect { Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    const Rectangle2D TreeRect   { Bounds.X + 8.0f, Y, Bounds.Width - 16.0f, std::max(60.0f, FooterRect.Y - Y - 4.0f) };

    if (ControlAppearance::QueryPointerOver(TreeRect, Pointer) && std::abs(WheelDeltaY) > 0.001f)
    {
        Session.OutlinerScrollY = std::max(0.0f, Session.OutlinerScrollY - WheelDeltaY * 28.0f);
    }

    // 5. Tree List (Primitives / Recipe / Fragments)
    Surface.PushClip(TreeRect);
    float Ty = TreeRect.Y - Session.OutlinerScrollY;
    const std::string Query = Session.OutlinerSearch.Text;

    auto DrawGroupHeader = [&](uint32_t GroupIdx, const char* Title, const char* Sub, int IconKind) -> bool
    {
        const Rectangle2D Row { TreeRect.X, Ty, TreeRect.Width, 42.0f };
        const bool Hov = ControlAppearance::QueryPointerOver(Row, Pointer) && TreeRect.Contains({ Pointer.X, Pointer.Y });
        if (Hov)
        {
            Surface.FillRectangle(Row, ColorRgba { 255u, 255u, 255u, 8u }, 11.0f);
        }
        if (Session.GroupExpanded[GroupIdx])
        {
            FractureIcons::DrawChevronDownGlyph(Surface, { Row.X + 4.0f, Row.Center().Y - 7.0f, 14.0f, 14.0f }, ColorRgba { 111u, 111u, 109u, 255u });
        }
        else
        {
            FractureIcons::DrawChevronRightGlyph(Surface, { Row.X + 4.0f, Row.Center().Y - 7.0f, 14.0f, 14.0f }, ColorRgba { 111u, 111u, 109u, 255u });
        }

        const Rectangle2D IconBox { Row.X + 24.0f, Row.Center().Y - 13.0f, 26.0f, 26.0f };
        if (IconKind == 0)
        {
            FractureIcons::DrawFolderSceneIcon(Surface, IconBox);
        }
        else if (IconKind == 1)
        {
            FractureIcons::DrawMaterialSphereIcon(Surface, { IconBox.X + 2.0f, IconBox.Y + 2.0f, 22.0f, 22.0f },
                MaterialLinearToSrgbSwatch(Session.ActiveMaterialSpec().ExteriorLinearRgb), false);
        }
        else
        {
            FractureIcons::DrawCollectionIcon(Surface, IconBox);
        }

        Surface.Text({ Row.X + 58.0f, Row.Y + 7.0f }, 12.2f, ColorRgba { 228u, 228u, 226u, 255u }, Title);
        Surface.Text({ Row.X + 58.0f, Row.Y + 23.0f }, 9.8f, ColorRgba { 132u, 132u, 130u, 255u }, Sub);

        if (Hov && Pointer.Pressed && !Session.FilterDropdownOpen)
        {
            Session.GroupExpanded[GroupIdx] = !Session.GroupExpanded[GroupIdx];
        }
        Ty += 42.0f;
        return Session.GroupExpanded[GroupIdx];
    };

    // Group 0: Primitives (9 analytical shapes)
    if (DrawGroupHeader(0u, "Primitives", "9 analytical shapes", 0))
    {
        const auto& Prims = FractureWorkspaceSession::InspectPrimitives();
        for (uint32_t I = 0u; I < FractureWorkspaceSession::PrimitiveCount; ++I)
        {
            const auto& Prim = Prims[I];
            if (!MatchesSearch(Prim.Title, Query)) continue;
            const Rectangle2D Row { TreeRect.X, Ty, TreeRect.Width, 38.0f };
            const bool Active = (Session.ActiveShape == Prim.Shape && Session.RenderOptions.SelectedFragmentIndex < 0);
            const bool SelectedShape = (Session.ActiveShape == Prim.Shape);
            const bool Hov = ControlAppearance::QueryPointerOver(Row, Pointer) && TreeRect.Contains({ Pointer.X, Pointer.Y });

            if (Active)
            {
                Surface.FillRectangle(Row, ColorRgba { 29u, 29u, 29u, 255u }, 10.0f);
                Surface.StrokeRectangle(Row, ColorRgba { 255u, 255u, 255u, 18u }, 10.0f, 1.0f);
                Surface.FillRectangle({ Row.X - 4.0f, Row.Y + 3.0f, 3.0f, Row.Height - 6.0f }, ColorRgba { 242u, 169u, 59u, 255u }, 1.5f);
            }
            else if (Hov)
            {
                Surface.FillRectangle(Row, ColorRgba { 255u, 255u, 255u, 9u }, 10.0f);
            }

            FractureIcons::DrawPrimitiveIcon(Surface, { Row.X + 31.0f, Row.Center().Y - 10.0f, 20.0f, 20.0f }, Prim.Shape, SelectedShape ? 1.0f : 0.72f);
            Surface.Text({ Row.X + 60.0f, Row.Y + 5.5f }, 11.6f, ColorRgba { 226u, 226u, 224u, 255u }, Prim.Title);
            const std::string Desc = SelectedShape ? Session.FormatPrimitiveSubtitle() : PrimitiveShortDescription(Prim.Shape);
            Surface.Text({ Row.X + 60.0f, Row.Y + 21.0f }, 9.6f, ColorRgba { 132u, 132u, 130u, 255u }, Desc.c_str());

            if (SelectedShape)
            {
                const Point2D MarkC { Row.Right() - 18.0f, Row.Center().Y };
                Surface.FillCircle(MarkC, 9.0f, ColorRgba { 52u, 199u, 89u, 255u });
                FractureIcons::DrawCheckGlyph(Surface, { MarkC.X - 5.5f, MarkC.Y - 5.5f, 11.0f, 11.0f }, ColorRgba { 11u, 36u, 18u, 255u }, 2.6f);
            }

            if (Hov && Pointer.Pressed && !Session.FilterDropdownOpen)
            {
                Session.RenderOptions.SelectedFragmentIndex = -1;
                Session.ApplyPrimitiveShape(Prim.Shape);
            }
            Ty += 38.0f;
        }
    }

    // Group 1: Recipe
    {
        char RecSub[48] {};
        std::snprintf(RecSub, sizeof(RecSub), "%s · %.2f kJ", Session.ActiveMaterialSpec().ShortLabel, Session.ImpactEnergyJ / 1000.0);
        if (DrawGroupHeader(1u, "Recipe", RecSub, 1))
        {
            const Rectangle2D Row { TreeRect.X, Ty, TreeRect.Width, 38.0f };
            Surface.Text({ Row.X + 31.0f, Row.Y + 8.0f }, 10.6f, ColorRgba { 216u, 216u, 214u, 255u },
                Session.MethodMode == FractureMethodMode::Baked ? "Baked pre-fracture" : "Realtime impact");
            Surface.Text({ Row.X + 31.0f, Row.Y + 22.0f }, 8.2f, ColorRgba { 108u, 108u, 106u, 255u },
                FractureWorkspaceSession::QuerySolverShortLabel(Session.SolverMethod));
            Ty += 38.0f;
        }
    }

    // Group 2: Fragments
    {
        char FragSub[48] {};
        std::snprintf(FragSub, sizeof(FragSub), "%zu pieces · %u released",
            Session.PartitionResult.Fragments.size(), Session.CountReleasedFragments());
        if (DrawGroupHeader(2u, "Fragments", FragSub, 2))
        {
            uint32_t Shown = 0u;
            const uint32_t Cap = Session.ShowAllFragments ? static_cast<uint32_t>(Session.PartitionResult.Fragments.size()) : 12u;
            for (const auto& Frag : Session.PartitionResult.Fragments)
            {
                const bool Released = Session.QueryFragmentReleased(Frag.Index);
                const bool Visible  = (Frag.Index < Session.FragmentVisibleFlags.size())
                    ? Session.FragmentVisibleFlags[Frag.Index] : true;
                if (Session.OutlinerFilter == FractureOutlinerFilter::Released && !Released) continue;
                if (Session.OutlinerFilter == FractureOutlinerFilter::Held && Released) continue;
                if (Session.OutlinerFilter == FractureOutlinerFilter::Hidden && Visible) continue;
                if (!MatchesSearch(Frag.Token, Query)) continue;
                if (Shown >= Cap) break;
                ++Shown;

                const Rectangle2D Row { TreeRect.X, Ty, TreeRect.Width, 38.0f };
                const bool Selected = (Session.RenderOptions.SelectedFragmentIndex == static_cast<int32_t>(Frag.Index));
                const bool Hov = ControlAppearance::QueryPointerOver(Row, Pointer) && TreeRect.Contains({ Pointer.X, Pointer.Y });

                if (Selected)
                {
                    Surface.FillRectangle(Row, ColorRgba { 29u, 29u, 29u, 255u }, 10.0f);
                    Surface.StrokeRectangle(Row, ColorRgba { 255u, 255u, 255u, 18u }, 10.0f, 1.0f);
                    Surface.FillRectangle({ Row.X - 4.0f, Row.Y + 3.0f, 3.0f, Row.Height - 6.0f }, ColorRgba { 242u, 169u, 59u, 255u }, 1.5f);
                }
                else if (Hov)
                {
                    Surface.FillRectangle(Row, ColorRgba { 255u, 255u, 255u, 9u }, 10.0f);
                }

                uint8_t SwR = 0u, SwG = 0u, SwB = 0u;
                if (Session.RenderOptions.RenderMode == FractureRenderViewMode::Cells)
                {
                    FractureViewportRenderer::SampleCellSwatchSrgb(Frag.Index, SwR, SwG, SwB);
                }
                else
                {
                    FractureViewportRenderer::SampleEnergySwatchSrgb(Frag.Proximity, SwR, SwG, SwB);
                }
                const Rectangle2D SwBox { Row.X + 34.0f, Row.Center().Y - 7.0f, 14.0f, 14.0f };
                Surface.FillRectangle(SwBox, { SwR, SwG, SwB, static_cast<uint8_t>(Visible ? 255u : 90u) }, 5.0f);
                Surface.StrokeRectangle(SwBox, ColorRgba { 255u, 255u, 255u, 32u }, 5.0f, 0.8f);

                Surface.Text({ Row.X + 58.0f, Row.Y + 5.5f }, 11.6f, ColorRgba { 226u, 226u, 224u, static_cast<uint8_t>(Visible ? 255u : 110u) }, Frag.Token.c_str());
                char Meta[48] {};
                std::snprintf(Meta, sizeof(Meta), "%.1f kg · %.0f cm",
                    Frag.MassKg, static_cast<double>(std::max({ Frag.Span.X, Frag.Span.Y, Frag.Span.Z })) * 100.0);
                Surface.Text({ Row.X + 58.0f, Row.Y + 21.0f }, 9.6f, ColorRgba { 132u, 132u, 130u, 255u }, Meta);

                char EnergyTxt[24] {};
                std::snprintf(EnergyTxt, sizeof(EnergyTxt), "%.0f J", Session.QueryFragmentReleaseShareJ(Frag.Index));
                Surface.TextRightAligned({ Row.X, Row.Center().Y - 5.5f, Row.Width - 58.0f, 13.0f }, 10.2f, ColorRgba { 154u, 154u, 152u, 255u }, EnergyTxt);

                const Rectangle2D EyeBtn { Row.Right() - 50.0f, Row.Center().Y - 10.0f, 20.0f, 20.0f };
                const bool EyeHov = ControlAppearance::QueryPointerOver(EyeBtn, Pointer);
                if (Hov || Selected || !Visible)
                {
                    FractureIcons::DrawEyeGlyph(Surface, { EyeBtn.X + 3.0f, EyeBtn.Y + 3.0f, 14.0f, 14.0f },
                        EyeHov ? ColorRgba { 230u, 230u, 228u, 255u } : ColorRgba { 106u, 106u, 104u, 255u }, Visible);
                }

                const Point2D MarkC { Row.Right() - 18.0f, Row.Center().Y };
                if (Released)
                {
                    Surface.FillCircle(MarkC, 9.0f, ColorRgba { 52u, 199u, 89u, 255u });
                    FractureIcons::DrawCheckGlyph(Surface, { MarkC.X - 5.5f, MarkC.Y - 5.5f, 11.0f, 11.0f }, ColorRgba { 11u, 36u, 18u, 255u }, 2.6f);
                }
                else
                {
                    Surface.FillCircle(MarkC, 9.0f, ColorRgba { 38u, 38u, 38u, 255u });
                    Surface.FillCircle(MarkC, 3.0f, ColorRgba { 90u, 90u, 88u, 255u });
                }

                if (EyeHov && Pointer.Pressed && !Session.FilterDropdownOpen)
                {
                    if (Frag.Index < Session.FragmentVisibleFlags.size())
                    {
                        Session.FragmentVisibleFlags[Frag.Index] = !Visible;
                        ++Session.SessionRevision;
                    }
                }
                else if (Hov && Pointer.Pressed && !Session.FilterDropdownOpen)
                {
                    Session.RenderOptions.SelectedFragmentIndex = Selected ? -1 : static_cast<int32_t>(Frag.Index);
                }
                Ty += 38.0f;
            }
        }
    }
    Surface.PopClip();

    // Open Filter Option Sheet (drawn over tree)
    if (Session.FilterDropdownOpen)
    {
        uint32_t Chosen = static_cast<uint32_t>(Session.OutlinerFilter);
        if (DropdownSelector::ConstructOptionSheet(
                Surface, DropRect, FilterLabels.data(), static_cast<uint32_t>(FilterLabels.size()),
                Chosen, Pointer, Chosen).Pressed)
        {
            Session.OutlinerFilter = static_cast<FractureOutlinerFilter>(Chosen % 5u);
            Session.FilterDropdownOpen = false;
        }
    }

    // Bottom 46px 4-slot Footer
    Surface.FillRectangle(FooterRect, ColorRgba { 15u, 15u, 15u, 255u }, 0.0f);
    Surface.StrokeLine({ FooterRect.X, FooterRect.Y }, { FooterRect.Right(), FooterRect.Y }, ColorRgba { 255u, 255u, 255u, 16u }, 1.0f);
    const float ColW = (FooterRect.Width - 28.0f) / 4.0f;
    char FpsBuf[24] {}, BakeBuf[24] {}, TrisBuf[24] {}, SeedBuf[24] {};
    std::snprintf(FpsBuf, sizeof(FpsBuf), "%.0f fps", std::clamp(1000.0f / std::max(1.0f, Session.LastFrameTimeMs), 1.0f, 120.0f));
    std::snprintf(BakeBuf, sizeof(BakeBuf), "%.0f ms", Session.LastTimings.TotalMs);
    std::snprintf(TrisBuf, sizeof(TrisBuf), "%u", Session.LastDrawnTriangles);
    std::snprintf(SeedBuf, sizeof(SeedBuf), "%u", Session.Seed);

    auto DrawFootCell = [&](uint32_t Col, const char* K, const char* V)
    {
        const float Fx = FooterRect.X + 16.0f + static_cast<float>(Col) * ColW;
        Surface.Text({ Fx, FooterRect.Y + 7.0f }, 9.4f, ColorRgba { 136u, 136u, 134u, 255u }, K);
        Surface.Text({ Fx, FooterRect.Y + 22.0f }, 11.8f, ColorRgba { 226u, 226u, 224u, 255u }, V);
    };
    DrawFootCell(0u, "REALTIME", FpsBuf);
    DrawFootCell(1u, "BAKE", BakeBuf);
    DrawFootCell(2u, "TRIS", TrisBuf);
    DrawFootCell(3u, "SEED", SeedBuf);
}

} // namespace Frontier::Workspaces::Fracture
