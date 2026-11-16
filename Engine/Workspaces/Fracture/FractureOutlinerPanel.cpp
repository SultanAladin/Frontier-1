/*============================================================================================================================================
                                                         FRACTUREOUTLINERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureOutlinerPanel: solid shapes, presets, fragment filter, shard visibility eye & selection highlight

#include "Engine/Workspaces/Fracture/FractureOutlinerPanel.h"

#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace ParticleAppearance;

bool ContainsInsensitive(std::string_view Haystack, std::string_view Needle) noexcept
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

const char* QuerySolidLabel(GeometrySpace::Fracture::FractureSolidCategory Cat) noexcept
{
    using CatEnum = GeometrySpace::Fracture::FractureSolidCategory;
    switch (Cat)
    {
        case CatEnum::Monolith:     return "Monolith";
        case CatEnum::CurtainPane:  return "Pane";
        case CatEnum::TimberBeam:   return "Beam";
        case CatEnum::Cylinder:     return "Cylinder";
        case CatEnum::Cone:         return "Cone";
        case CatEnum::Boulder:      return "Boulder";
        case CatEnum::HollowCavity: return "Cavity";
        case CatEnum::Cube:         return "Cube";
        default:                    return "Solid";
    }
}

} // namespace

void FractureOutlinerPanel::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    FractureWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     WheelDeltaY,
    float                     Opacity) noexcept
{
    if (Bounds.Width < 180.0f || Bounds.Height < 200.0f || Opacity <= 0.001f)
    {
        return;
    }

    const auto C = QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg, 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 22.0f, 1.0f);

    const float PadX   = 14.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 14.0f;

    // Header
    Surface.Text({ Bounds.X + PadX, Y }, 11.5f, C.TextPrimary, "Specimen & Shards");
    char CountBuf[48] {};
    std::snprintf(CountBuf, sizeof(CountBuf), "%u pcs", static_cast<unsigned>(Session.PartitionResult.Fragments.size()));
    Surface.TextRightAligned({ Bounds.X + PadX, Y, InnerW, 14.0f }, 10.0f, C.TextMuted, CountBuf);
    Y += 22.0f;

    // Solid shape grid (4x2 pills)
    Surface.Text({ Bounds.X + PadX, Y }, 9.2f, C.TextMuted, "SOLID PRIMITIVE");
    Y += 14.0f;
    const float ChipGap = 4.0f;
    const float ChipW   = (InnerW - ChipGap * 3.0f) * 0.25f;
    const float ChipH   = 22.0f;
    for (uint32_t I = 0u; I < 8u; ++I)
    {
        const uint32_t Col = I % 4u;
        const uint32_t Row = I / 4u;
        const auto Cat = static_cast<GeometrySpace::Fracture::FractureSolidCategory>(I);
        const Rectangle2D ChipRect {
            Bounds.X + PadX + static_cast<float>(Col) * (ChipW + ChipGap),
            Y + static_cast<float>(Row) * (ChipH + ChipGap),
            ChipW, ChipH
        };
        const bool Active  = (Session.SolidCategory == Cat);
        const bool Hovered = ControlAppearance::QueryPointerOver(ChipRect, Pointer);
        Surface.FillRectangle(ChipRect, Active ? C.CardElevated : (Hovered ? C.CardBg : C.WellBg), 7.0f);
        Surface.StrokeRectangle(ChipRect, Active ? C.WarmAccent : C.Hairline, 7.0f, 1.0f);
        Surface.TextCentered(ChipRect, 9.2f, Active ? C.TextPrimary : C.TextSoft, QuerySolidLabel(Cat));
        if (Hovered && Pointer.Pressed)
        {
            Session.SolidCategory = Cat;
            Session.RebuildSolidAndPartition();
        }
    }
    Y += (ChipH + ChipGap) * 2.0f + 8.0f;

    // Execution Mode pills (Realtime / Baked / Hybrid Bonds)
    Surface.Text({ Bounds.X + PadX, Y }, 9.2f, C.TextMuted, "RELEASE MODE");
    Y += 14.0f;
    static constexpr std::array<const char*, 3> ModeLabels = { "Realtime", "Baked", "Bonds" };
    const float ModeW = (InnerW - 8.0f) / 3.0f;
    for (uint32_t M = 0u; M < 3u; ++M)
    {
        const auto Mode = static_cast<GeometrySpace::Fracture::FractureExecutionMode>(M);
        const Rectangle2D PillRect { Bounds.X + PadX + static_cast<float>(M) * (ModeW + 4.0f), Y, ModeW, 22.0f };
        const bool Active  = (Session.ExecutionMode == Mode);
        const bool Hovered = ControlAppearance::QueryPointerOver(PillRect, Pointer);
        Surface.FillRectangle(PillRect, Active ? C.WarmSoft : (Hovered ? C.CardBg : C.WellBg), 7.0f);
        Surface.StrokeRectangle(PillRect, Active ? C.WarmAccent : C.Hairline, 7.0f, 1.0f);
        Surface.TextCentered(PillRect, 9.2f, Active ? C.WarmAccent : C.TextSoft, ModeLabels[M]);
        if (Hovered && Pointer.Pressed)
        {
            Session.ExecutionMode = Mode;
            if (Mode == GeometrySpace::Fracture::FractureExecutionMode::Baked)
            {
                Session.SolverMethod = GeometrySpace::Fracture::FractureSolverMethod::EngineeringBakedSolid;
                Session.ExecuteFracturePartitionOnly();
            }
            ++Session.RevisionCounter;
        }
    }
    Y += 30.0f;

    // Search field
    const Rectangle2D SearchRect { Bounds.X + PadX, Y, InnerW, 24.0f };
    const bool SearchHover = ControlAppearance::QueryPointerOver(SearchRect, Pointer);
    Surface.FillRectangle(SearchRect, C.WellBg, 8.0f);
    Surface.StrokeRectangle(
        SearchRect,
        Session.SearchFieldFocused ? C.WarmAccent : (SearchHover ? C.HairlineStrong : C.Hairline),
        8.0f, 1.0f);
    if (Pointer.Pressed)
    {
        Session.SearchFieldFocused = SearchHover;
    }
    if (Session.SearchFieldFocused)
    {
        char Typed[32] {};
        const uint32_t N = Surface.ReadTypedUtf8(Typed, sizeof(Typed));
        if (N > 0u)
        {
            for (uint32_t K = 0u; K < N; ++K)
            {
                if (Typed[K] >= 32 && Typed[K] < 127)
                {
                    Session.SearchFilterText.push_back(Typed[K]);
                }
            }
        }
        if (Surface.QueryKeyPressed(InterfaceKey::Backspace) && !Session.SearchFilterText.empty())
        {
            Session.SearchFilterText.pop_back();
        }
    }
    Surface.Text(
        { SearchRect.X + 9.0f, SearchRect.Y + 6.0f },
        9.8f,
        Session.SearchFilterText.empty() ? C.TextFaint : C.TextPrimary,
        Session.SearchFilterText.empty() ? "Filter shards (FRAG-001...)" : Session.SearchFilterText.c_str());
    Y += 32.0f;

    // Fragment list viewport
    const float FooterH = 34.0f;
    const Rectangle2D ListRect { Bounds.X + PadX, Y, InnerW, std::max(40.0f, Bounds.Bottom() - FooterH - Y) };
    Surface.PushClip(ListRect);

    const float RowH = 26.0f;
    std::vector<uint32_t> VisibleIndices;
    VisibleIndices.reserve(Session.PartitionResult.Fragments.size());
    for (uint32_t I = 0u; I < Session.PartitionResult.Fragments.size(); ++I)
    {
        if (ContainsInsensitive(Session.PartitionResult.Fragments[I].Token, Session.SearchFilterText))
        {
            VisibleIndices.push_back(I);
        }
    }

    const float TotalListH = static_cast<float>(VisibleIndices.size()) * (RowH + 3.0f);
    const float MaxScroll  = std::max(0.0f, TotalListH - ListRect.Height);
    if (ControlAppearance::QueryPointerOver(ListRect, Pointer))
    {
        ScrollOffsetPx -= WheelDeltaY * 26.0f;
    }
    ScrollOffsetPx = std::clamp(ScrollOffsetPx, 0.0f, MaxScroll);

    float RowY = ListRect.Y - ScrollOffsetPx;
    for (uint32_t FragIdx : VisibleIndices)
    {
        if (RowY + RowH >= ListRect.Y && RowY <= ListRect.Bottom())
        {
            auto& Frag = Session.PartitionResult.Fragments[FragIdx];
            const Rectangle2D RowRect { ListRect.X, RowY, ListRect.Width, RowH };
            const bool Selected = (Session.RenderOptions.SelectedFragmentIndex == static_cast<int32_t>(Frag.Index));
            const bool Hovered  = ControlAppearance::QueryPointerOver(RowRect, Pointer) &&
                                  ControlAppearance::QueryPointerOver(ListRect, Pointer);

            Surface.FillRectangle(RowRect, Selected ? C.CardElevated : (Hovered ? C.CardBg : C.WellBg), 7.0f);
            Surface.StrokeRectangle(RowRect, Selected ? C.WarmAccent : C.Hairline, 7.0f, 1.0f);

            const Rectangle2D EyeRect { RowRect.Right() - 24.0f, RowRect.Y + 4.0f, 18.0f, 18.0f };
            const bool EyeHover = ControlAppearance::QueryPointerOver(EyeRect, Pointer);
            Surface.FillCircle(
                EyeRect.Center(),
                3.5f,
                Frag.VisibilityEnabled ? C.LiveGreen : C.TextFaint);
            if (EyeHover && Pointer.Pressed)
            {
                Frag.VisibilityEnabled = !Frag.VisibilityEnabled;
                ++Session.RevisionCounter;
            }
            else if (Hovered && Pointer.Pressed)
            {
                Session.RenderOptions.SelectedFragmentIndex = Selected ? -1 : static_cast<int32_t>(Frag.Index);
                ++Session.RevisionCounter;
            }

            Surface.Text({ RowRect.X + 8.0f, RowRect.Y + 7.0f }, 9.8f, Selected ? C.WarmAccent : C.TextPrimary, Frag.Token.c_str());
            char StatBuf[40] {};
            std::snprintf(StatBuf, sizeof(StatBuf), "%.1f kg", Frag.MassKg);
            Surface.TextRightAligned({ RowRect.X + 64.0f, RowRect.Y + 7.0f, RowRect.Width - 94.0f, 14.0f }, 9.2f, C.TextMuted, StatBuf);
        }
        RowY += RowH + 3.0f;
    }

    Surface.PopClip();

    // Footer summary
    const Rectangle2D FooterRect { Bounds.X + PadX, Bounds.Bottom() - 26.0f, InnerW, 18.0f };
    char SummaryBuf[64] {};
    std::snprintf(
        SummaryBuf,
        sizeof(SummaryBuf),
        "Bonds %u/%u · Drift %.1e",
        Session.PartitionResult.Receipt.IntactBondCount,
        Session.PartitionResult.Receipt.BondCount,
        Session.PartitionResult.Receipt.VolumeDriftRatio);
    Surface.Text(FooterRect.Center().X > 0.0f ? Point2D { FooterRect.X, FooterRect.Y } : Point2D {}, 9.0f, C.TextMuted, SummaryBuf);
}

} // namespace Frontier::Workspaces::Fracture
