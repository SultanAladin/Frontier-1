/*============================================================================================================================================
                                                            BAKINGTARGETSPANE.CPP
============================================================================================================================================*/
// 🧩 Implementation of BakingTargetsPane: 4 quick-select preset chips + 2-column categorized 18-target swatch tiles.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Texturing/BakingTargetsPane.h"

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

using namespace TexturingAppearance;

namespace
{

[[nodiscard]] bool PointInsideRect(Rectangle2D Rect, Point2D Cursor) noexcept
{
    return Cursor.X >= Rect.X && Cursor.X <= Rect.Right() && Cursor.Y >= Rect.Y && Cursor.Y <= Rect.Bottom();
}

} // namespace

BakingTargetsPane::~BakingTargetsPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void BakingTargetsPane::AttachSession(BakingWorkspaceSession* TargetSession) noexcept
{
    Session       = TargetSession;
    ScrollOffsetY = 0.0f;
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
    bool          Ready) const noexcept
{
    Surface.PushClip(SwatchBounds);

    const float Cx = SwatchBounds.X + SwatchBounds.Width * 0.5f;
    const float Cy = SwatchBounds.Y + SwatchBounds.Height * 0.5f;
    const float R  = std::min(SwatchBounds.Width, SwatchBounds.Height) * 0.36f;

    switch (Target)
    {
        case BakerTargetId::VolumeSdf:
        {
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 20u, 36u, 58u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R * 1.15f, ColorRgba{ 46u, 82u, 134u, 255u });
            Surface.FillCircle({ Cx, Cy }, R * 0.85f, ColorRgba{ 232u, 238u, 200u, 255u });
            Surface.FillCircle({ Cx, Cy }, R * 0.62f, ColorRgba{ 165u, 72u, 46u, 255u });
            Surface.TextCentered(SwatchBounds, 9.0f, ColorRgba{ 238u, 241u, 246u, 255u }, Ready ? "SDF OK" : "SDF");
            break;
        }
        case BakerTargetId::TangentNormal:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 118u, 126u, 238u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R, ColorRgba{ 138u, 146u, 250u, 255u });
            Surface.FillCircle({ Cx - R * 0.25f, Cy - R * 0.25f }, R * 0.55f, ColorRgba{ 170u, 136u, 242u, 180u });
            break;
        case BakerTargetId::WorldNormal:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 62u, 118u, 182u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R, ColorRgba{ 122u, 204u, 152u, 255u });
            Surface.FillCircle({ Cx + R * 0.28f, Cy - R * 0.18f }, R * 0.45f, ColorRgba{ 216u, 116u, 142u, 180u });
            break;
        case BakerTargetId::ModelSpaceNormal:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 164u, 96u, 178u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R, ColorRgba{ 118u, 188u, 198u, 255u });
            Surface.FillCircle({ Cx - R * 0.24f, Cy - R * 0.24f }, R * 0.48f, ColorRgba{ 222u, 154u, 108u, 175u });
            break;
        case BakerTargetId::BentNormal:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 106u, 148u, 192u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R, ColorRgba{ 152u, 198u, 178u, 255u });
            Surface.FillCircle({ Cx + R * 0.22f, Cy + R * 0.20f }, R * 0.46f, ColorRgba{ 208u, 148u, 204u, 170u });
            break;
        case BakerTargetId::BevelNormal:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 98u, 106u, 224u, 255u }, 7.0f);
            Surface.StrokeRectangle(
                { Cx - R, Cy - R * 0.85f, R * 2.0f, R * 1.7f },
                ColorRgba{ 192u, 198u, 255u, 235u },
                4.0f,
                2.0f);
            break;
        case BakerTargetId::AmbientOcclusion:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 194u, 198u, 204u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R * 1.04f, ColorRgba{ 126u, 130u, 136u, 255u });
            Surface.FillCircle({ Cx, Cy }, R * 0.62f, ColorRgba{ 222u, 225u, 230u, 255u });
            break;
        case BakerTargetId::SecondaryAo:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 138u, 142u, 148u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R * 0.96f, ColorRgba{ 72u, 76u, 82u, 255u });
            Surface.FillCircle({ Cx, Cy }, R * 0.56f, ColorRgba{ 182u, 186u, 192u, 255u });
            break;
        case BakerTargetId::Curvature:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 98u, 104u, 114u, 255u }, 7.0f);
            Surface.FillCircle({ Cx - R * 0.25f, Cy }, R * 0.72f, ColorRgba{ 88u, 196u, 162u, 230u });
            Surface.FillCircle({ Cx + R * 0.30f, Cy }, R * 0.52f, ColorRgba{ 224u, 118u, 98u, 210u });
            break;
        case BakerTargetId::Convexity:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 32u, 36u, 42u, 255u }, 7.0f);
            Surface.StrokeCircle({ Cx, Cy }, R * 0.85f, ColorRgba{ 224u, 232u, 238u, 240u }, 2.2f);
            break;
        case BakerTargetId::Concavity:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 32u, 36u, 42u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R * 0.80f, ColorRgba{ 204u, 94u, 76u, 230u });
            break;
        case BakerTargetId::Thickness:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 72u, 38u, 26u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R * 0.92f, ColorRgba{ 218u, 142u, 72u, 255u });
            Surface.FillCircle({ Cx - R * 0.20f, Cy - R * 0.16f }, R * 0.44f, ColorRgba{ 246u, 206u, 134u, 240u });
            break;
        case BakerTargetId::HeightDisplacement:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 58u, 60u, 66u, 255u }, 7.0f);
            Surface.FillCircle({ Cx, Cy }, R * 0.96f, ColorRgba{ 168u, 172u, 180u, 255u });
            Surface.FillCircle({ Cx, Cy }, R * 0.48f, ColorRgba{ 238u, 240u, 245u, 255u });
            break;
        case BakerTargetId::TopDownDust:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 62u, 66u, 72u, 255u }, 7.0f);
            Surface.FillRectangle(
                { SwatchBounds.X, SwatchBounds.Y, SwatchBounds.Width, SwatchBounds.Height * 0.48f },
                ColorRgba{ 204u, 186u, 148u, 235u },
                0.0f);
            break;
        case BakerTargetId::UvIslandId:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 56u, 128u, 184u, 255u }, 7.0f);
            Surface.FillRectangle(
                { SwatchBounds.X + 4.0f, SwatchBounds.Y + 4.0f, SwatchBounds.Width * 0.44f, SwatchBounds.Height * 0.44f },
                ColorRgba{ 218u, 114u, 88u, 255u },
                4.0f);
            Surface.FillRectangle(
                { SwatchBounds.X + SwatchBounds.Width * 0.50f, SwatchBounds.Y + SwatchBounds.Height * 0.42f, SwatchBounds.Width * 0.42f, SwatchBounds.Height * 0.46f },
                ColorRgba{ 112u, 198u, 134u, 255u },
                4.0f);
            break;
        case BakerTargetId::PartMaterialId:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 104u, 74u, 152u, 255u }, 7.0f);
            Surface.FillCircle({ Cx - R * 0.42f, Cy }, R * 0.56f, ColorRgba{ 232u, 176u, 68u, 255u });
            Surface.FillCircle({ Cx + R * 0.42f, Cy }, R * 0.56f, ColorRgba{ 76u, 196u, 184u, 255u });
            break;
        case BakerTargetId::WorldPosition:
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 92u, 156u, 112u, 255u }, 7.0f);
            Surface.FillCircle({ Cx - R * 0.25f, Cy + R * 0.16f }, R * 0.65f, ColorRgba{ 214u, 88u, 108u, 200u });
            Surface.FillCircle({ Cx + R * 0.28f, Cy - R * 0.18f }, R * 0.62f, ColorRgba{ 88u, 146u, 232u, 210u });
            break;
        case BakerTargetId::UvWireframe:
        default:
        {
            Surface.FillRectangle(SwatchBounds, ColorRgba{ 24u, 26u, 32u, 255u }, 7.0f);
            const ColorRgba WireGold{ 214u, 178u, 96u, 210u };
            for (int32_t Step = 1; Step <= 3; ++Step)
            {
                const float Fx = SwatchBounds.X + SwatchBounds.Width * (static_cast<float>(Step) * 0.25f);
                const float Fy = SwatchBounds.Y + SwatchBounds.Height * (static_cast<float>(Step) * 0.25f);
                Surface.StrokeLine({ Fx, SwatchBounds.Y + 4.0f }, { Fx, SwatchBounds.Bottom() - 4.0f }, WireGold, 1.0f);
                Surface.StrokeLine({ SwatchBounds.X + 4.0f, Fy }, { SwatchBounds.Right() - 4.0f, Fy }, WireGold, 1.0f);
            }
            Surface.StrokeLine(
                { SwatchBounds.X + 6.0f, SwatchBounds.Y + 6.0f },
                { SwatchBounds.Right() - 6.0f, SwatchBounds.Bottom() - 6.0f },
                WireGold,
                1.0f);
            break;
        }
    }

    Surface.PopClip();
}

void BakingTargetsPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Session == nullptr || Bounds.Width < 60.0f || Bounds.Height < 120.0f)
    {
        return;
    }

    const TexturingThemeColors Theme   = QueryTexturingThemeColors();
    const Point2D              Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed = Input.PrimaryPointer.Pressed;

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground, 0.0f);

    // Top Heading Bar
    constexpr float PadX       = 14.0f;
    constexpr float HeaderTopY = 12.0f;
    const float     InnerW     = std::max(40.0f, Bounds.Width - PadX * 2.0f);
    char            CountMeta[32];
    std::snprintf(CountMeta, sizeof(CountMeta), "%zu MAPS", BakingWorkspaceSession::TargetCount);
    const float HeaderConsumed = ConstructPaneHeadingBar(Surface, Bounds.X + PadX, Bounds.Y + HeaderTopY, InnerW, "Bake Map Targets", CountMeta, Theme);

    // Bottom Status Footer
    constexpr float   FooterH = 26.0f;
    const Rectangle2D FooterBounds{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    ConstructPaneStatusFooter(Surface, FooterBounds, "GTX BVH4 / RTX RayQuery", "High -> Low Cage", Theme);

    // Scrollable region
    const float       ScrollTop = Bounds.Y + HeaderTopY + HeaderConsumed;
    const Rectangle2D ScrollViewport{
        Bounds.X,
        ScrollTop,
        Bounds.Width,
        std::max(1.0f, FooterBounds.Y - ScrollTop),
    };

    if (PointInsideRect(ScrollViewport, Cursor) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetY = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScrollY);
    }

    Surface.PushClip(ScrollViewport);
    const float InnerX = ScrollViewport.X + PadX;
    float       CurY   = ScrollViewport.Y + 8.0f - ScrollOffsetY;

    // Quick preset chips
    struct PresetRowEntry
    {
        const char*      Label;
        BakerQuickPreset Preset;
    };
    const std::array<PresetRowEntry, 4u> Presets {{
        { "PBR Core",     BakerQuickPreset::PbrStandard },
        { "PBR + Volume", BakerQuickPreset::PbrPlusVolume },
        { "Normals",      BakerQuickPreset::Normals },
        { "Select All",   BakerQuickPreset::SelectAll },
    }};

    int32_t ActivePresetIdx = -1;
    for (std::size_t I = 0u; I < Presets.size(); ++I)
    {
        if (Session->QueryPresetMatched(Presets[I].Preset))
        {
            ActivePresetIdx = static_cast<int32_t>(I);
            break;
        }
    }
    const std::array<const char*, 4u> PresetLabels {{ Presets[0].Label, Presets[1].Label, Presets[2].Label, Presets[3].Label }};
    const ChipStripOutcome PresetOutcome = ConstructWrappedChipStrip(
        Surface,
        InnerX,
        CurY,
        InnerW,
        PresetLabels,
        ActivePresetIdx,
        Theme,
        Cursor,
        Pressed,
        22.0f,
        9.2f);
    if (PresetOutcome.ClickedIndex >= 0 && PresetOutcome.ClickedIndex < static_cast<int32_t>(Presets.size()))
    {
        Session->ApplyQuickPreset(Presets[static_cast<std::size_t>(PresetOutcome.ClickedIndex)].Preset);
    }
    CurY += PresetOutcome.ConsumedHeight + 12.0f;

    // 4 Categorized groups with 2-column target swatch tiles
    const auto&     Registry    = BakingWorkspaceSession::QueryTargetRegistry();
    constexpr float TileGap    = 8.0f;
    const float     TileWidth  = std::max(60.0f, (InnerW - TileGap) * 0.5f);
    constexpr float TileHeight = 96.0f;

    for (std::size_t GroupIdx = 0u; GroupIdx < static_cast<std::size_t>(BakerTargetGroup::Count); ++GroupIdx)
    {
        const BakerTargetGroup Group = static_cast<BakerTargetGroup>(GroupIdx);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::SemiBold,
            { InnerX + 2.0f, CurY + 2.0f },
            InnerW,
            9.5f,
            Theme.TextMuted,
            BakingWorkspaceSession::QueryGroupHeading(Group));
        CurY += 20.0f;

        std::size_t ColumnIdx = 0u;
        for (const BakerTargetDescriptor& Desc : Registry)
        {
            if (Desc.Group != Group)
            {
                continue;
            }
            const float       TileX = InnerX + static_cast<float>(ColumnIdx % 2u) * (TileWidth + TileGap);
            const float       TileY = CurY + static_cast<float>(ColumnIdx / 2u) * (TileHeight + TileGap);
            const Rectangle2D TileRect{ TileX, TileY, TileWidth, TileHeight };

            const bool Checked   = Session->SelectedTargets[static_cast<std::size_t>(Desc.Id)];
            const bool Inspected = Session->InspectedTarget == Desc.Id;
            const bool Ready     = Session->QueryTargetReady(Desc.Id);
            const bool Hovered   = PointInsideRect(TileRect, Cursor) && PointInsideRect(ScrollViewport, Cursor);

            const ColorRgba TileBg = Inspected
                ? Theme.AccentSoft
                : (Hovered ? Theme.HoverSurface : Theme.WellSurface);
            const ColorRgba TileBorder = Inspected
                ? Theme.AccentBorder
                : (Hovered ? Theme.BorderStrong : Theme.BorderSubtle);

            Surface.FillRectangle(TileRect, TileBg, 10.0f);
            Surface.StrokeRectangle(TileRect, TileBorder, 10.0f, Inspected ? 1.5f : 1.0f);

            const Rectangle2D SwatchRect{ TileRect.X + 7.0f, TileRect.Y + 7.0f, TileRect.Width - 14.0f, 44.0f };
            RenderTargetSwatchGraphic(Surface, SwatchRect, Desc.Id, Ready);

            // Top-right selection toggle check circle inside swatch corner
            const Rectangle2D CheckRect{ SwatchRect.Right() - 20.0f, SwatchRect.Y + 3.0f, 17.0f, 17.0f };
            const bool        CheckHovered = PointInsideRect(CheckRect, Cursor) && PointInsideRect(ScrollViewport, Cursor);
            const ColorRgba   CheckBg      = Checked
                ? Theme.HighlightFill
                : (CheckHovered ? Theme.HoverSurface : ColorRgba{ 16u, 18u, 22u, 225u });
            const ColorRgba   CheckBorder  = Checked ? Theme.HighlightFill : Theme.BorderStrong;
            Surface.FillCircle({ CheckRect.X + 8.5f, CheckRect.Y + 8.5f }, 8.5f, CheckBg);
            Surface.StrokeCircle({ CheckRect.X + 8.5f, CheckRect.Y + 8.5f }, 8.5f, CheckBorder, 1.0f);
            if (Checked)
            {
                WorkspaceAppearance::ConstructCheckSymbol(
                    Surface,
                    { CheckRect.X + 8.5f, CheckRect.Y + 8.5f },
                    9.5f,
                    Theme.HighlightInk,
                    1.8f);
            }

            // Tile title & short caption
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::SemiBold,
                { TileRect.X + 8.0f, SwatchRect.Bottom() + 5.0f },
                TileRect.Width - 16.0f,
                10.0f,
                Theme.TextPrimary,
                Desc.Label);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                FontWeightClass::Regular,
                { TileRect.X + 8.0f, SwatchRect.Bottom() + 21.0f },
                TileRect.Width - 16.0f,
                9.0f,
                Ready ? Theme.ReadyGreen : Theme.TextMuted,
                Ready ? "Ready" : Desc.ShortSummary);

            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    TileRect,
                    OptionalTooltip::FromTitled(Desc.Label, "Click tile to inspect · Click top-right check to toggle bake output."));
                if (Pressed)
                {
                    if (CheckHovered)
                    {
                        Session->ToggleTargetSelected(Desc.Id);
                    }
                    else
                    {
                        Session->InspectTarget(Desc.Id);
                    }
                }
            }
            ++ColumnIdx;
        }

        const std::size_t RowCount = (ColumnIdx + 1u) / 2u;
        CurY += static_cast<float>(RowCount) * (TileHeight + TileGap) + 10.0f;
    }

    const float ContentBottom = CurY + ScrollOffsetY + 12.0f;
    MaxScrollY                = std::max(0.0f, ContentBottom - ScrollViewport.Bottom());
    ScrollOffsetY             = std::clamp(ScrollOffsetY, 0.0f, MaxScrollY);

    Surface.PopClip();
    Surface.PopClip();
}

} // namespace Frontier::Workspaces
