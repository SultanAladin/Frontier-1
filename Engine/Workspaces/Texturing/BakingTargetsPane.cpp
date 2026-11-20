/*==========================================================================================================================================
                                                            BAKINGTARGETSPANE.CPP
==========================================================================================================================================*/
// 🧩 Implementation of BakingTargetsPane: Project Zero BakeOutliner Queue tree with stat tiles, preset chips & expandable target groups.

#include "Engine/Workspaces/Texturing/BakingTargetsPane.h"

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <imgui.h>
#include <string>

namespace Frontier::Workspaces
{

namespace
{

using namespace TexturingAppearance;

[[nodiscard]] ColorRgba QueryBakeTargetSwatchColor(BakerTargetId Target) noexcept
{
    switch (Target)
    {
        case BakerTargetId::TangentNormal:      return { 127u, 127u, 255u, 255u };
        case BakerTargetId::ModelSpaceNormal:   return { 178u, 108u, 212u, 255u };
        case BakerTargetId::WorldNormal:        return { 104u, 206u, 168u, 255u };
        case BakerTargetId::BentNormal:         return { 138u, 166u, 240u, 255u };
        case BakerTargetId::BevelNormal:        return { 162u, 152u, 248u, 255u };
        case BakerTargetId::AmbientOcclusion:   return { 208u, 208u, 210u, 255u };
        case BakerTargetId::SecondaryAo:        return { 182u, 182u, 186u, 255u };
        case BakerTargetId::Curvature:          return { 176u, 176u, 180u, 255u };
        case BakerTargetId::Convexity:          return { 212u, 196u, 154u, 255u };
        case BakerTargetId::Concavity:          return { 148u, 148u, 152u, 255u };
        case BakerTargetId::Thickness:          return { 232u, 118u,  88u, 255u };
        case BakerTargetId::HeightDisplacement: return { 164u, 164u, 168u, 255u };
        case BakerTargetId::TopDownDust:        return { 216u, 202u, 174u, 255u };
        case BakerTargetId::PartMaterialId:     return { 224u,  96u, 136u, 255u };
        case BakerTargetId::UvIslandId:         return { 136u, 212u, 118u, 255u };
        case BakerTargetId::WorldPosition:      return { 216u, 148u,  92u, 255u };
        case BakerTargetId::UvWireframe:        return { 196u, 202u, 212u, 255u };
        case BakerTargetId::VolumeSdf:
        default:                                return {  88u, 198u, 218u, 255u };
    }
}

[[nodiscard]] uint32_t QueryBakeTargetGroupIndex(BakerTargetGroup Group) noexcept
{
    switch (Group)
    {
        case BakerTargetGroup::Normals:         return 1u;
        case BakerTargetGroup::Surface:         return 2u;
        case BakerTargetGroup::DiagnosticMasks: return 3u;
        case BakerTargetGroup::VolumeDistance:
        default:                                return 4u;
    }
}

[[nodiscard]] bool TargetMatchesSearch(const BakerTargetDescriptor& Desc, std::string_view Query) noexcept
{
    if (Query.empty())
    {
        return true;
    }
    auto ContainsCaseInsensitive = [](std::string_view Haystack, std::string_view Needle) noexcept -> bool
    {
        if (Needle.size() > Haystack.size()) return false;
        for (std::size_t I = 0u; I + Needle.size() <= Haystack.size(); ++I)
        {
            bool Match = true;
            for (std::size_t J = 0u; J < Needle.size(); ++J)
            {
                if (std::tolower(static_cast<unsigned char>(Haystack[I + J])) !=
                    std::tolower(static_cast<unsigned char>(Needle[J])))
                {
                    Match = false;
                    break;
                }
            }
            if (Match) return true;
        }
        return false;
    };
    return ContainsCaseInsensitive(Desc.Label, Query) || ContainsCaseInsensitive(Desc.ShortSummary, Query);
}

} // namespace

BakingTargetsPane::~BakingTargetsPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void BakingTargetsPane::AttachSession(BakingWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void BakingTargetsPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##BakingTargetsCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);
}

void BakingTargetsPane::RenderTargetSwatchGraphic(
    PixelSpace&   Surface,
    Rectangle2D   SwatchBounds,
    BakerTargetId Target,
    bool          /*Ready*/) const noexcept
{
    const ColorRgba Tint = QueryBakeTargetSwatchColor(Target);
    Surface.FillRectangle(SwatchBounds, Tint, 6.0f);
    Surface.StrokeRectangle(SwatchBounds, ColorRgba{ 255u, 255u, 255u, 42u }, 6.0f, 1.0f);
}

void BakingTargetsPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  /*DeltaSeconds*/)
{
    if (Session == nullptr || Bounds.Width < 120.0f || Bounds.Height < 120.0f)
    {
        return;
    }

    const TexturingThemeColors Theme = QueryTexturingTheme(Surface);
    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground);
    Surface.StrokeLine({ Bounds.Right() - 0.5f, Bounds.Y }, { Bounds.Right() - 0.5f, Bounds.Bottom() }, Theme.BorderSubtle, 1.0f);

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    const float   PadX    = 12.0f;
    const float   InnerW  = std::max(120.0f, Bounds.Width - PadX * 2.0f);
    float         CurY    = Bounds.Y + 12.0f;

    // 1. Header ("Queue", "MAP BAKING · HIGH -> LOW")
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { Bounds.X + PadX, CurY }, 14.0f, Theme.TextPrimary, "Queue");
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { Bounds.X + PadX, CurY + 18.0f }, InnerW, 8.2f, Theme.TextFaint, "MAP BAKING · HIGH -> LOW");
    CurY += 36.0f;

    // 2. Stat Tiles ("QUEUED 5/18", "READY 0/18")
    const std::size_t QueuedCount = Session->CountSelectedTargets();
    std::size_t ReadyCount = 0u;
    for (bool R : Session->BakedReadyTargets)
    {
        if (R) ++ReadyCount;
    }
    char QueuedValBuf[32];
    std::snprintf(QueuedValBuf, sizeof(QueuedValBuf), "%zu/%zu", QueuedCount, BakingWorkspaceSession::TargetCount);
    char ReadyValBuf[32];
    std::snprintf(ReadyValBuf, sizeof(ReadyValBuf), "%zu/%zu", ReadyCount, BakingWorkspaceSession::TargetCount);

    const float StatGap = 8.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    const Rectangle2D StatQueuedR{ Bounds.X + PadX, CurY, StatW, 42.0f };
    const Rectangle2D StatReadyR { StatQueuedR.Right() + StatGap, CurY, StatW, 42.0f };

    if (ConstructOutlinerStatTile(
            Surface,
            StatQueuedR,
            ControlIcon::LayersSlabs,
            "QUEUED",
            QueuedValBuf,
            QueuedCount > 0u,
            ActiveFilterIndex == 1u,
            Theme,
            Cursor,
            Pressed))
    {
        ActiveFilterIndex = (ActiveFilterIndex == 1u) ? 0u : 1u;
    }
    if (ConstructOutlinerStatTile(
            Surface,
            StatReadyR,
            ControlIcon::CircleCheck,
            "READY",
            ReadyValBuf,
            ReadyCount > 0u,
            ActiveFilterIndex == 2u,
            Theme,
            Cursor,
            Pressed))
    {
        ActiveFilterIndex = (ActiveFilterIndex == 2u) ? 0u : 2u;
    }
    CurY += 50.0f;

    // 3. Search Bar + Filter Button
    const float FilterBtnW = 64.0f;
    const Rectangle2D SearchBoxR{ Bounds.X + PadX, CurY, InnerW - FilterBtnW - 6.0f, 28.0f };
    const Rectangle2D FilterBtnR{ SearchBoxR.Right() + 6.0f, CurY, FilterBtnW, 28.0f };

    Surface.FillRectangle(SearchBoxR, Theme.WellSurface, 8.0f);
    Surface.StrokeRectangle(SearchBoxR, SearchBuffer.Active ? Theme.BorderStrong : Theme.BorderSubtle, 8.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchBoxR.X + 8.0f, SearchBoxR.Y + 7.5f, 13.0f, Theme.TextFaint, 1.65f });
    if (SearchBuffer.Length == 0u && !SearchBuffer.Active)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SearchBoxR.X + 26.0f, SearchBoxR.Y + 6.5f }, 9.4f, Theme.TextFaint, "Search targets...");
    }
    (void)TextInputField::Construct(
        Surface,
        { SearchBoxR.X + 22.0f, SearchBoxR.Y + 1.0f, SearchBoxR.Width - 26.0f, SearchBoxR.Height - 2.0f },
        SearchBuffer,
        Input.PrimaryPointer,
        9.6f);

    constexpr std::array<const char*, 4u> FilterLabels {{ "Filter", "Queued", "Ready", "Volume" }};
    if (ConstructHeaderActionCapsule(Surface, FilterBtnR, ControlIcon::Count, FilterLabels[ActiveFilterIndex % 4u], true, FilterSheetOpen, Theme, Cursor, Pressed))
    {
        FilterSheetOpen = !FilterSheetOpen;
    }
    CurY += 34.0f;

    // 4. Preset Chips Strip ("PBR Standard | PBR + Volume | Normals | All 18 | Clear")
    struct PresetChipSpec
    {
        const char*      Label;
        BakerQuickPreset Preset;
        bool             ClearAction;
    };
    constexpr std::array<PresetChipSpec, 5u> PresetChips {{
        { "PBR Standard", BakerQuickPreset::PbrStandard,   false },
        { "PBR + Volume", BakerQuickPreset::PbrPlusVolume, false },
        { "Normals",      BakerQuickPreset::Normals,       false },
        { "All 18",       BakerQuickPreset::SelectAll,     false },
        { "Clear",        BakerQuickPreset::PbrStandard,   true  },
    }};
    float ChipX = Bounds.X + PadX;
    float ChipY = CurY;
    for (const auto& Chip : PresetChips)
    {
        const float ChipW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.6f, Chip.Label) + 14.0f;
        if (ChipX + ChipW > Bounds.Right() - PadX && ChipX > Bounds.X + PadX)
        {
            ChipX = Bounds.X + PadX;
            ChipY += 24.0f;
        }
        const Rectangle2D ChipR{ ChipX, ChipY, ChipW, 21.0f };
        const bool ActiveChip = !Chip.ClearAction && Session->QueryPresetMatched(Chip.Preset);
        const bool HoverChip  = ChipR.Contains(Cursor);

        const ColorRgba FillCol = ActiveChip
            ? ColorRgba{ 240u, 240u, 238u, 255u }
            : (HoverChip ? Theme.HoverSurface : Theme.WellSurface);
        const ColorRgba InkCol  = ActiveChip
            ? ColorRgba{ 18u, 19u, 22u, 255u }
            : (HoverChip ? Theme.TextPrimary : Theme.TextMuted);
        Surface.FillRectangle(ChipR, FillCol, 6.0f);
        Surface.StrokeRectangle(ChipR, ActiveChip ? FillCol : Theme.BorderSubtle, 6.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, ActiveChip ? FontWeightClass::SemiBold : FontWeightClass::Medium, { ChipR.X + 7.0f, ChipR.Y + 3.5f }, 8.6f, InkCol, Chip.Label);

        if (!FilterSheetOpen && Pressed && HoverChip)
        {
            if (Chip.ClearAction)
            {
                Session->SelectedTargets.fill(false);
            }
            else
            {
                Session->ApplyQuickPreset(Chip.Preset);
            }
        }
        ChipX += ChipW + 5.0f;
    }
    CurY = ChipY + 26.0f;
    Surface.StrokeLine({ Bounds.X, CurY }, { Bounds.Right(), CurY }, Theme.BorderSubtle, 1.0f);
    CurY += 4.0f;

    // 5. Scrollable Queue Tree (Mesh Pair + 4 Target Groups)
    constexpr float TelemetryH = 28.0f;
    const Rectangle2D TreeViewportR{ Bounds.X, CurY, Bounds.Width, std::max(48.0f, Bounds.Bottom() - TelemetryH - CurY) };

    if (!FilterSheetOpen && TreeViewportR.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY = ControlAppearance::AdvanceScrollOffset(
            ScrollOffsetY,
            Input.WheelDeltaY * 32.0f,
            TreeViewportR.Height + MaxScrollY,
            TreeViewportR.Height);
    }
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, MaxScrollY);

    Surface.PushClip(TreeViewportR);
    float RowY = TreeViewportR.Y + 2.0f - ScrollOffsetY;
    constexpr float RowH = 38.0f;
    const bool AllowTreeClicks = !FilterSheetOpen && TreeViewportR.Contains(Cursor);

    // 5A. Mesh Pair Row ("Mesh Pair", "high -> low", badge "Utah Teapot")
    const auto& ActiveStudy = Session->QueryActiveStudyDescriptor();
    const OutlinerRowInteraction MeshPairAct = ConstructOutlinerTreeRow(
        Surface,
        { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, RowH },
        0u,
        true,
        GroupExpanded[0],
        false,
        false,
        ControlIcon::LayersSlabs,
        Theme.TextMuted,
        "Mesh Pair",
        "high -> low",
        {},
        ActiveStudy.Label,
        -1,
        -1,
        -1,
        Theme,
        Cursor,
        AllowTreeClicks && Pressed,
        false);
    if (MeshPairAct.CaretToggled || MeshPairAct.RowSelected)
    {
        GroupExpanded[0] = !GroupExpanded[0];
    }
    RowY += RowH + 2.0f;

    if (GroupExpanded[0])
    {
        char HighMeta[64];
        std::snprintf(HighMeta, sizeof(HighMeta), "HIGH · %u tris", Session->QueryActiveHighTriangles());
        (void)ConstructOutlinerTreeRow(
            Surface,
            { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, 34.0f },
            1u,
            false,
            false,
            false,
            false,
            ControlIcon::CubeShape,
            Theme.TextPrimary,
            !Session->CustomHighAssetLabel.empty() ? Session->CustomHighAssetLabel : std::string(ActiveStudy.Label) + " (High)",
            HighMeta,
            {},
            "READY",
            -1,
            -1,
            -1,
            Theme,
            Cursor,
            false,
            false);
        RowY += 35.0f;

        char LowMeta[64];
        std::snprintf(LowMeta, sizeof(LowMeta), "LOW · %u tris", Session->QueryActiveLowTriangles());
        (void)ConstructOutlinerTreeRow(
            Surface,
            { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, 34.0f },
            1u,
            false,
            false,
            false,
            false,
            ControlIcon::CubeShape,
            Theme.TextPrimary,
            !Session->CustomLowAssetLabel.empty() ? Session->CustomLowAssetLabel : std::string(ActiveStudy.Label) + " (Low)",
            LowMeta,
            {},
            "UVS",
            -1,
            -1,
            -1,
            Theme,
            Cursor,
            false,
            false);
        RowY += 35.0f;
    }

    // 5B. Four Target Groups: Normals (1), Surface & AO (2), IDs & Utility (3), Volume (4)
    constexpr std::array<const char*, 5u> GroupTitles {{
        "Mesh Pair",
        "Normals",
        "Surface & AO",
        "IDs & Utility",
        "Volume",
    }};
    const auto& TargetCatalog = BakingWorkspaceSession::QueryTargetRegistry();
    char ResTagBuf[16];
    if (Session->TargetResolutionPx >= 1024u)
    {
        std::snprintf(ResTagBuf, sizeof(ResTagBuf), "%uK", Session->TargetResolutionPx / 1024u);
    }
    else
    {
        std::snprintf(ResTagBuf, sizeof(ResTagBuf), "%u", Session->TargetResolutionPx);
    }
    char VolTagBuf[16];
    std::snprintf(VolTagBuf, sizeof(VolTagBuf), "%u^3", Session->VolumeGridDim);

    const std::string_view QueryView(SearchBuffer.Text, SearchBuffer.Length);

    for (uint32_t G = 1u; G <= 4u; ++G)
    {
        std::size_t GroupTotal  = 0u;
        std::size_t GroupQueued = 0u;
        std::size_t GroupShown  = 0u;

        for (std::size_t T = 0u; T < BakingWorkspaceSession::TargetCount; ++T)
        {
            const auto& Desc = TargetCatalog[T];
            if (QueryBakeTargetGroupIndex(Desc.Group) != G)
            {
                continue;
            }
            ++GroupTotal;
            if (Session->SelectedTargets[T]) ++GroupQueued;

            if (ActiveFilterIndex == 1u && !Session->SelectedTargets[T])   continue;
            if (ActiveFilterIndex == 2u && !Session->BakedReadyTargets[T]) continue;
            if (ActiveFilterIndex == 3u && !Desc.VolumeTarget)             continue;
            if (!TargetMatchesSearch(Desc, QueryView))                     continue;
            ++GroupShown;
        }
        if (GroupShown == 0u)
        {
            continue;
        }

        char GroupMeta[32];
        std::snprintf(GroupMeta, sizeof(GroupMeta), "%zu/%zu", GroupQueued, GroupTotal);
        const OutlinerRowInteraction GroupAct = ConstructOutlinerTreeRow(
            Surface,
            { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, RowH },
            0u,
            true,
            GroupExpanded[G],
            false,
            false,
            (G == 4u) ? ControlIcon::CubeShape : ControlIcon::LayersSlabs,
            Theme.TextMuted,
            GroupTitles[G],
            GroupMeta,
            {},
            {},
            -1,
            -1,
            -1,
            Theme,
            Cursor,
            AllowTreeClicks && Pressed,
            false);
        if (GroupAct.CaretToggled || GroupAct.RowSelected)
        {
            GroupExpanded[G] = !GroupExpanded[G];
        }
        RowY += RowH + 2.0f;

        if (!GroupExpanded[G])
        {
            continue;
        }

        for (std::size_t T = 0u; T < BakingWorkspaceSession::TargetCount; ++T)
        {
            const auto& Desc = TargetCatalog[T];
            if (QueryBakeTargetGroupIndex(Desc.Group) != G)
            {
                continue;
            }
            if (ActiveFilterIndex == 1u && !Session->SelectedTargets[T])   continue;
            if (ActiveFilterIndex == 2u && !Session->BakedReadyTargets[T]) continue;
            if (ActiveFilterIndex == 3u && !Desc.VolumeTarget)             continue;
            if (!TargetMatchesSearch(Desc, QueryView))                     continue;

            const bool IsSelected = (Session->InspectedTarget == Desc.Id);
            const bool IsQueued   = Session->SelectedTargets[T];
            const bool IsReady    = Session->BakedReadyTargets[T];

            Rectangle2D IconSlotR {};
            const OutlinerRowInteraction TargetAct = ConstructOutlinerTreeRow(
                Surface,
                { Bounds.X + 8.0f, RowY, Bounds.Width - 16.0f, RowH },
                1u,
                false,
                false,
                IsSelected,
                !IsQueued,
                ControlIcon::Count,
                Theme.TextPrimary,
                Desc.Label,
                Desc.ShortSummary,
                {},
                IsReady ? "BAKED" : (Desc.VolumeTarget ? VolTagBuf : ResTagBuf),
                -1,
                IsQueued ? 1 : 0,
                -1,
                Theme,
                Cursor,
                AllowTreeClicks && Pressed,
                false,
                &IconSlotR);

            RenderTargetSwatchGraphic(Surface, IconSlotR, Desc.Id, IsReady);

            if (TargetAct.StatusToggled)
            {
                Session->ToggleTargetSelected(Desc.Id);
            }
            else if (TargetAct.RowSelected)
            {
                Session->InspectTarget(Desc.Id);
            }
            RowY += RowH + 2.0f;
        }
    }

    const float ContentH = (RowY + ScrollOffsetY) - TreeViewportR.Y + 8.0f;
    MaxScrollY = std::max(0.0f, ContentH - TreeViewportR.Height);
    Surface.PopClip();

    // 6. Bottom Telemetry Strip ("RES 512 | QUEUED 5 | READY 0 | RAYS 60")
    const Rectangle2D TelemetryR{ Bounds.X, Bounds.Bottom() - TelemetryH, Bounds.Width, TelemetryH };
    char QueuedStr[16];
    std::snprintf(QueuedStr, sizeof(QueuedStr), "%zu", QueuedCount);
    char ReadyStr[16];
    std::snprintf(ReadyStr, sizeof(ReadyStr), "%zu", ReadyCount);
    char RaysStr[16];
    std::snprintf(RaysStr, sizeof(RaysStr), "%d", static_cast<int>(QueuedCount) * Session->AoSampleCount);

    const std::array<std::pair<std::string_view, std::string_view>, 4u> TelemetryCols {{
        { "RES",    ResTagBuf },
        { "QUEUED", QueuedStr },
        { "READY",  ReadyStr  },
        { "RAYS",   RaysStr   },
    }};
    ConstructOutlinerTelemetryStrip(Surface, TelemetryR, TelemetryCols, Theme);

    if (FilterSheetOpen)
    {
        constexpr std::array<const char*, 4u> FilterMenuNames {{
            "All targets",
            "Queued only",
            "Ready only",
            "Volume only",
        }};
        const Rectangle2D SheetR{ FilterBtnR.Right() - 126.0f, FilterBtnR.Bottom() + 4.0f, 126.0f, 112.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(I) * 25.0f, SheetR.Width - 10.0f, 23.0f };
            const bool Active = (ActiveFilterIndex == I);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            WorkspaceAppearance::ConstructWeightedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 4.0f }, 9.4f, Theme.TextPrimary, FilterMenuNames[I]);
            if (Pressed && Hover)
            {
                ActiveFilterIndex = I;
                FilterSheetOpen   = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !FilterBtnR.Contains(Cursor))
        {
            FilterSheetOpen = false;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
