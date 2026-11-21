/*==========================================================================================================================================
                                                            BAKINGTARGETSPANE.CPP
==========================================================================================================================================*/
// 🧩 1:1 implementation of BakingTargetsPane matching Experimental/TextureStudio docked Bake Outliner (cards-bake.png):
//    Queue header, QUEUED / UNBAKED KPI tiles, Search + Filter pill row, PBR Core / PBR + Volume / Normals / All / None preset chips,
//    hierarchical Study -> Group -> Map tree with 24x24 shaded 3D sphere thumbnails, action bar, and 4-column telemetry footer.

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
        case BakerTargetId::TangentNormal:      return { 126u, 134u, 248u, 255u };
        case BakerTargetId::ModelSpaceNormal:   return { 178u, 108u, 212u, 255u };
        case BakerTargetId::WorldNormal:        return { 104u, 206u, 168u, 255u };
        case BakerTargetId::BentNormal:         return { 142u, 168u, 242u, 255u };
        case BakerTargetId::BevelNormal:        return { 162u, 152u, 248u, 255u };
        case BakerTargetId::AmbientOcclusion:   return { 188u, 190u, 196u, 255u };
        case BakerTargetId::SecondaryAo:        return { 168u, 170u, 176u, 255u };
        case BakerTargetId::Curvature:          return { 172u, 174u, 180u, 255u };
        case BakerTargetId::Convexity:          return { 196u, 186u, 158u, 255u };
        case BakerTargetId::Concavity:          return { 144u, 146u, 152u, 255u };
        case BakerTargetId::Thickness:          return { 162u, 164u, 170u, 255u };
        case BakerTargetId::HeightDisplacement: return { 158u, 160u, 166u, 255u };
        case BakerTargetId::TopDownDust:        return { 206u, 196u, 174u, 255u };
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
    // 24x24 dark rounded well with shaded 3D sphere inside (matches cards-bake.png 1:1)
    Surface.FillRectangle(SwatchBounds, ColorRgba{ 16u, 17u, 20u, 255u }, 6.0f);
    Surface.StrokeRectangle(SwatchBounds, ColorRgba{ 255u, 255u, 255u, 24u }, 6.0f, 1.0f);

    const Point2D C{ SwatchBounds.X + SwatchBounds.Width * 0.5f, SwatchBounds.Y + SwatchBounds.Height * 0.5f };
    const float   R = std::min(SwatchBounds.Width, SwatchBounds.Height) * 0.34f;
    const ColorRgba Base = QueryBakeTargetSwatchColor(Target);
    const ColorRgba Rim {
        static_cast<uint8_t>(Base.Red   * 0.55f),
        static_cast<uint8_t>(Base.Green * 0.55f),
        static_cast<uint8_t>(Base.Blue  * 0.62f),
        255u
    };
    const ColorRgba Spec {
        static_cast<uint8_t>(std::min(255, static_cast<int>(Base.Red)   + 46)),
        static_cast<uint8_t>(std::min(255, static_cast<int>(Base.Green) + 46)),
        static_cast<uint8_t>(std::min(255, static_cast<int>(Base.Blue)  + 46)),
        255u
    };
    Surface.FillCircle(C, R, Rim);
    Surface.FillCircle({ C.X - R * 0.14f, C.Y - R * 0.14f }, R * 0.78f, Base);
    Surface.FillCircle({ C.X - R * 0.28f, C.Y - R * 0.28f }, R * 0.40f, Spec);
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
    Surface.FillRectangle(Bounds, Theme.LightSurface ? Theme.PaneBackground : ColorRgba{ 24u, 25u, 28u, 255u });

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    const float   PadX    = 12.0f;
    const float   LeftX   = Bounds.X + PadX;
    const float   InnerW  = std::max(120.0f, Bounds.Width - PadX * 2.0f);
    float         CurY    = Bounds.Y + 12.0f;

    // 1. Header ("Queue  Map Baking · 18 maps + volume" + Play icon circle)
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, CurY - 2.0f }, 15.0f, Theme.TextPrimary, "Queue");
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX + 48.0f, CurY + 3.0f }, InnerW - 76.0f, 8.6f, Theme.TextFaint,
        "Map Baking · 18 maps + volume");

    const Rectangle2D TopRunR{ Bounds.Right() - PadX - 24.0f, CurY - 1.0f, 24.0f, 24.0f };
    Surface.FillRectangle(TopRunR, TopRunR.Contains(Cursor) ? ColorRgba{ 44u, 45u, 49u, 255u } : ColorRgba{ 34u, 35u, 39u, 255u }, 12.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::BakeBox, VectorIconPlacement{ TopRunR.X + 6.5f, TopRunR.Y + 6.5f, 11.0f, Theme.TextMuted, 1.65f });
    if (Pressed && TopRunR.Contains(Cursor))
    {
        Session->ExecuteBakeSimulation();
    }
    CurY += 30.0f;

    // 2. Two KPI Stat Tiles ("QUEUED 5/18" & "UNBAKED 0/5") — matches cards-bake.png 1:1
    const std::size_t QueuedCount = Session->CountSelectedTargets();
    std::size_t ReadyCount = 0u;
    for (bool R : Session->BakedReadyTargets)
    {
        if (R) ++ReadyCount;
    }
    char QueuedValBuf[32];
    std::snprintf(QueuedValBuf, sizeof(QueuedValBuf), "%zu/%zu", QueuedCount, BakingWorkspaceSession::TargetCount);
    char ReadyValBuf[32];
    std::snprintf(ReadyValBuf, sizeof(ReadyValBuf), "%zu/%zu", ReadyCount, std::max<std::size_t>(1u, QueuedCount));

    const float StatGap = 8.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    const Rectangle2D StatQueuedR{ LeftX, CurY, StatW, 42.0f };
    const Rectangle2D StatReadyR { StatQueuedR.Right() + StatGap, CurY, StatW, 42.0f };

    if (ConstructOutlinerStatTile(
            Surface, StatQueuedR, ControlIcon::LayersSlabs, "QUEUED", QueuedValBuf, QueuedCount > 0u, ActiveFilterIndex == 1u, Theme, Cursor, Pressed))
    {
        ActiveFilterIndex = (ActiveFilterIndex == 1u) ? 0u : 1u;
    }
    if (ConstructOutlinerStatTile(
            Surface, StatReadyR, ControlIcon::CircleCheck, ReadyCount > 0u ? "BAKED" : "UNBAKED", ReadyValBuf, ReadyCount > 0u, ActiveFilterIndex == 2u, Theme, Cursor, Pressed))
    {
        ActiveFilterIndex = (ActiveFilterIndex == 2u) ? 0u : 2u;
    }
    CurY += 50.0f;

    // 3. Search Bar + Filter Button ("Search 18 maps..." + "Filter v")
    const float FilterBtnW = 68.0f;
    const Rectangle2D SearchBoxR{ LeftX, CurY, InnerW - FilterBtnW - 6.0f, 28.0f };
    const Rectangle2D FilterBtnR{ SearchBoxR.Right() + 6.0f, CurY, FilterBtnW, 28.0f };

    Surface.FillRectangle(SearchBoxR, ColorRgba{ 19u, 20u, 23u, 255u }, 14.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchBoxR.X + 10.0f, SearchBoxR.Y + 8.0f, 12.0f, Theme.TextFaint, 1.6f });
    if (SearchBuffer.Length == 0u && !SearchBuffer.Active)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SearchBoxR.X + 27.0f, SearchBoxR.Y + 6.5f }, 9.0f, Theme.TextFaint, "Search 18 maps...");
    }
    (void)TextInputField::Construct(
        Surface,
        { SearchBoxR.X + 24.0f, SearchBoxR.Y + 1.0f, SearchBoxR.Width - 28.0f, SearchBoxR.Height - 2.0f },
        SearchBuffer,
        Input.PrimaryPointer,
        9.2f);

    constexpr std::array<const char*, 4u> FilterLabels {{ "Filter", "Queued", "Ready", "Volume" }};
    if (ConstructHeaderActionCapsule(Surface, FilterBtnR, ControlIcon::Count, FilterLabels[ActiveFilterIndex % 4u], true, FilterSheetOpen, Theme, Cursor, Pressed))
    {
        FilterSheetOpen = !FilterSheetOpen;
    }
    CurY += 35.0f;

    // 4. Preset Chips Strip ("PBR Core | PBR + Volume | Normals | All | None") — matches cards-bake.png 1:1
    struct PresetChipSpec
    {
        const char*      Label;
        BakerQuickPreset Preset;
        bool             ClearAction;
    };
    constexpr std::array<PresetChipSpec, 5u> PresetChips {{
        { "PBR Core",     BakerQuickPreset::PbrStandard,   false },
        { "PBR + Volume", BakerQuickPreset::PbrPlusVolume, false },
        { "Normals",      BakerQuickPreset::Normals,       false },
        { "All",          BakerQuickPreset::SelectAll,     false },
        { "None",         BakerQuickPreset::PbrStandard,   true  },
    }};
    float ChipX = LeftX;
    float ChipY = CurY;
    for (const auto& Chip : PresetChips)
    {
        const float ChipW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.2f, Chip.Label) + 16.0f;
        if (ChipX + ChipW > Bounds.Right() - PadX && ChipX > LeftX)
        {
            ChipX = LeftX;
            ChipY += 24.0f;
        }
        const Rectangle2D ChipR{ ChipX, ChipY, ChipW, 20.0f };
        const bool ActiveChip = !Chip.ClearAction && Session->QueryPresetMatched(Chip.Preset);
        const bool HoverChip  = ChipR.Contains(Cursor);

        const ColorRgba FillCol = ActiveChip
            ? ColorRgba{ 42u, 44u, 50u, 255u }
            : (HoverChip ? ColorRgba{ 38u, 40u, 46u, 255u } : ColorRgba{ 30u, 31u, 36u, 255u });
        Surface.FillRectangle(ChipR, FillCol, 10.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { ChipR.X + 8.0f, ChipR.Y + 3.5f }, 8.2f,
            ActiveChip || HoverChip ? Theme.TextPrimary : Theme.TextMuted, Chip.Label);

        if (!FilterSheetOpen && Pressed && HoverChip)
        {
            if (Chip.ClearAction) Session->SelectedTargets.fill(false);
            else                  Session->ApplyQuickPreset(Chip.Preset);
        }
        ChipX += ChipW + 5.0f;
    }
    CurY = ChipY + 26.0f;

    // 5. Scrollable Queue Tree (Study Root + 4 Target Groups)
    constexpr float ActionBarH = 30.0f;
    constexpr float TelemetryH = 32.0f;
    const Rectangle2D TreeViewportR{ Bounds.X, CurY, Bounds.Width, std::max(48.0f, Bounds.Bottom() - ActionBarH - TelemetryH - CurY) };

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
    constexpr float RowH = 36.0f;
    const bool AllowTreeClicks = !FilterSheetOpen && TreeViewportR.Contains(Cursor);

    // 5A. Study Root Row ("Utah Teapot" / "Standard Study · 6.4k tris · Sub-D Tri" + "512" badge)
    const auto& ActiveStudy = Session->QueryActiveStudyDescriptor();
    char ResTagBuf[16];
    std::snprintf(ResTagBuf, sizeof(ResTagBuf), "%u", Session->TargetResolutionPx);
    {
        const OutlinerRowInteraction StudyAct = ConstructOutlinerTreeRow(
            Surface,
            { LeftX, RowY, InnerW, RowH },
            0u,
            true,
            true,
            false,
            false,
            ControlIcon::CubeShape,
            Theme.TextMuted,
            ActiveStudy.Label,
            "Standard Study · 6.4k tris · Sub-D Tri",
            {},
            ResTagBuf,
            -1, -1, -1,
            Theme, Cursor, AllowTreeClicks && Pressed, false);
        (void)StudyAct;
        RowY += RowH + 2.0f;
    }

    // 5B. Four Target Groups: Normals (1), Surface (2), Masks & IDs (3), Volume & Distance (4)
    constexpr std::array<const char*, 5u> GroupTitles {{
        "Study",
        "Normals",
        "Surface",
        "Masks & IDs",
        "Volume & Distance",
    }};
    const auto& TargetCatalog = BakingWorkspaceSession::QueryTargetRegistry();
    const std::string_view QueryView(SearchBuffer.Text, SearchBuffer.Length);

    for (uint32_t G = 1u; G <= 4u; ++G)
    {
        std::size_t GroupTotal  = 0u;
        std::size_t GroupQueued = 0u;
        std::size_t GroupShown  = 0u;

        for (std::size_t T = 0u; T < BakingWorkspaceSession::TargetCount; ++T)
        {
            const auto& Desc = TargetCatalog[T];
            if (QueryBakeTargetGroupIndex(Desc.Group) != G) continue;
            ++GroupTotal;
            if (Session->SelectedTargets[T]) ++GroupQueued;

            if (ActiveFilterIndex == 1u && !Session->SelectedTargets[T])   continue;
            if (ActiveFilterIndex == 2u && !Session->BakedReadyTargets[T]) continue;
            if (ActiveFilterIndex == 3u && !Desc.VolumeTarget)             continue;
            if (!TargetMatchesSearch(Desc, QueryView))                     continue;
            ++GroupShown;
        }
        if (GroupShown == 0u) continue;

        char GroupMeta[48], GroupTag[16];
        std::snprintf(GroupMeta, sizeof(GroupMeta), "%zu/%zu queued · %.1f MB", GroupQueued, GroupTotal, static_cast<float>(GroupQueued) * 1.0f);
        std::snprintf(GroupTag, sizeof(GroupTag), "%zu/%zu", GroupQueued, GroupTotal);

        const OutlinerRowInteraction GroupAct = ConstructOutlinerTreeRow(
            Surface,
            { LeftX, RowY, InnerW, 34.0f },
            1u,
            true,
            GroupExpanded[G],
            false,
            false,
            (G == 1u) ? ControlIcon::MaterialSphere : ((G == 2u) ? ControlIcon::SunIllumination : ControlIcon::LayersSlabs),
            Theme.TextMuted,
            GroupTitles[G],
            GroupMeta,
            {},
            GroupTag,
            -1, -1, -1,
            Theme, Cursor, AllowTreeClicks && Pressed, false);
        if (GroupAct.CaretToggled || GroupAct.RowSelected)
        {
            GroupExpanded[G] = !GroupExpanded[G];
        }
        RowY += 35.0f;

        if (!GroupExpanded[G]) continue;

        for (std::size_t T = 0u; T < BakingWorkspaceSession::TargetCount; ++T)
        {
            const auto& Desc = TargetCatalog[T];
            if (QueryBakeTargetGroupIndex(Desc.Group) != G) continue;
            if (ActiveFilterIndex == 1u && !Session->SelectedTargets[T])   continue;
            if (ActiveFilterIndex == 2u && !Session->BakedReadyTargets[T]) continue;
            if (ActiveFilterIndex == 3u && !Desc.VolumeTarget)             continue;
            if (!TargetMatchesSearch(Desc, QueryView))                     continue;

            const bool IsSelected = (Session->InspectedTarget == Desc.Id);
            const bool IsQueued   = Session->SelectedTargets[T];
            const bool IsReady    = Session->BakedReadyTargets[T];

            char SubSummary[80];
            if (IsQueued)
            {
                std::snprintf(SubSummary, sizeof(SubSummary), "%s · 1.0 MB · 377 k rays", Desc.ShortSummary);
            }
            else
            {
                std::snprintf(SubSummary, sizeof(SubSummary), "%s · 1.0 MB", Desc.ShortSummary);
            }

            Rectangle2D IconSlotR {};
            const OutlinerRowInteraction TargetAct = ConstructOutlinerTreeRow(
                Surface,
                { LeftX, RowY, InnerW, RowH },
                2u,
                false,
                false,
                IsSelected,
                false,
                ControlIcon::Count,
                Theme.TextPrimary,
                Desc.Label,
                SubSummary,
                {},
                "",
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
            RowY += RowH + 1.0f;
        }
    }

    const float ContentH = (RowY + ScrollOffsetY) - TreeViewportR.Y + 8.0f;
    MaxScrollY = std::max(0.0f, ContentH - TreeViewportR.Height);
    Surface.PopClip();

    // 6. Action Bar above Footer (+ | ^ | v | Eye ... Trash, matching cards-bake.png)
    const float ActY = Bounds.Bottom() - ActionBarH - TelemetryH;
    static constexpr std::array<ControlIcon, 4u> LeftIcons {{
        ControlIcon::PlusAdd,
        ControlIcon::ChevronUp,
        ControlIcon::ChevronDown,
        ControlIcon::EyeVisible,
    }};
    for (std::size_t I = 0u; I < LeftIcons.size(); ++I)
    {
        const Rectangle2D BtnR{ LeftX + static_cast<float>(I) * 26.0f, ActY + 3.0f, 24.0f, 24.0f };
        ConstructHeaderIconButton(Surface, BtnR, LeftIcons[I], false, true, Theme, Cursor, Pressed);
    }
    const Rectangle2D TrashR{ Bounds.Right() - PadX - 24.0f, ActY + 3.0f, 24.0f, 24.0f };
    ConstructHeaderIconButton(Surface, TrashR, ControlIcon::TrashDelete, false, false, Theme, Cursor, Pressed);

    // 7. Bottom 4-Column Telemetry Strip ("OUTPUT 4.2 MB | RAYS 6.7 M | ETA 141 ms | DEVICE GPU", matching cards-bake.png)
    const Rectangle2D TelemetryR{ Bounds.X + 8.0f, Bounds.Bottom() - TelemetryH - 4.0f, Bounds.Width - 16.0f, TelemetryH };
    char OutStr[24];
    std::snprintf(OutStr, sizeof(OutStr), "%.1f MB", std::max(1.0f, static_cast<float>(QueuedCount) * 0.84f));
    const std::array<std::pair<std::string_view, std::string_view>, 4u> TelemetryCols {{
        { "OUTPUT", OutStr   },
        { "RAYS",   "6.7 M"  },
        { "ETA",    "141 ms" },
        { "DEVICE", "GPU"    },
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
        Surface.FillRectangle(SheetR, ColorRgba{ 32u, 33u, 36u, 255u }, 10.0f);
        Surface.StrokeRectangle(SheetR, ColorRgba{ 255u, 255u, 255u, 28u }, 10.0f, 1.0f);
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 6.0f, SheetR.Y + 6.0f + static_cast<float>(I) * 25.0f, SheetR.Width - 12.0f, 23.0f };
            const bool Act = (ActiveFilterIndex == I);
            const bool Hov = RowR.Contains(Cursor);
            if (Act || Hov)
            {
                Surface.FillRectangle(RowR, Act ? Theme.AccentSoft : ColorRgba{ 44u, 46u, 52u, 255u }, 6.0f);
            }
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 4.0f }, RowR.Width - 16.0f, 9.4f,
                Act || Hov ? Theme.TextPrimary : Theme.TextMuted, FilterMenuNames[I]);
            if (Hov && Input.PrimaryPointer.Released)
            {
                ActiveFilterIndex = I;
                FilterSheetOpen   = false;
            }
        }
        if (Input.PrimaryPointer.Released && !SheetR.Contains(Cursor) && !FilterBtnR.Contains(Cursor))
        {
            FilterSheetOpen = false;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
