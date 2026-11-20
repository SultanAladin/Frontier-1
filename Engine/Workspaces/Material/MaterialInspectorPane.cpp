/*============================================================================================================================================
                                                        MATERIALINSPECTORPANE.CPP
============================================================================================================================================*/
// 🧩 Implementation of MaterialInspectorPane: OpenPBR 1.1 header, 4-tile telemetry (F0, CLASS, GLOSS, ALBEDO), Tiling & UV card,
//    and 6-group 28-channel stack with side-by-side multi-open channel editors and zero amber borders.

#include "Engine/Workspaces/Material/MaterialInspectorPane.h"

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

using namespace TexturingAppearance;

MaterialInspectorPane::~MaterialInspectorPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void MaterialInspectorPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##MaterialInspectorCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, ActiveSliderId >= 0);
}

void MaterialInspectorPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Outliner == nullptr || Bounds.Width < 180.0f || Bounds.Height < 180.0f)
    {
        return;
    }

    const TexturingThemeColors Theme   = QueryTexturingThemeColors();
    const Point2D              Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed = Input.PrimaryPointer.Pressed;

    if (!Input.PrimaryPointer.Down)
    {
        ActiveSliderId = -1;
    }

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground, 0.0f);
    Surface.StrokeLine({ Bounds.X, Bounds.Y }, { Bounds.X, Bounds.Bottom() }, Theme.BorderSubtle, 1.0f);

    const float PadX   = 12.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float       CurY   = Bounds.Y + 12.0f;

    // 1. Project Zero Card Header ("Material Inspector  OPENPBR 1.1" + Preset pill + Refresh + Export)
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::MaterialSphere, VectorIconPlacement{ LeftX, CurY + 3.0f, 13.5f, ColorRgba{ 232u, 194u, 122u, 255u }, 1.7f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { LeftX + 19.0f, CurY + 1.0f }, 10.6f, Theme.TextPrimary, "Material Inspector");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { LeftX + 124.0f, CurY + 3.0f }, 7.8f, ColorRgba{ 232u, 194u, 122u, 255u }, "OPENPBR 1.1");

    const char* ActiveMatTitle = (Session != nullptr && !Session->DocumentTabs.empty())
        ? Session->InspectActiveRecipe().DisplayTitle.c_str()
        : "Glazed Terracotta";

    const Rectangle2D ExpBtnR{ Bounds.Right() - PadX - 24.0f, CurY - 2.0f, 24.0f, 24.0f };
    const Rectangle2D RefBtnR{ ExpBtnR.X - 26.0f, CurY - 2.0f, 24.0f, 24.0f };
    const Rectangle2D PresetR{ std::max(LeftX + 186.0f, RefBtnR.X - 118.0f), CurY - 2.0f, 114.0f, 24.0f };

    Surface.FillRectangle(PresetR, PresetR.Contains(Cursor) ? Theme.HoverSurface : Theme.SectionSurface, 7.0f);
    Surface.StrokeRectangle(PresetR, Theme.BorderSubtle, 7.0f, 1.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { PresetR.X + 8.0f, PresetR.Y + 5.0f }, PresetR.Width - 22.0f, 8.2f, Theme.TextPrimary, ActiveMatTitle);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ChevronDown, VectorIconPlacement{ PresetR.Right() - 14.0f, PresetR.Y + 7.5f, 9.0f, Theme.TextMuted, 1.6f });
    if (Pressed && PresetR.Contains(Cursor) && Session != nullptr && !Session->PresetRepository.empty())
    {
        const std::size_t NextIdx = (Session->ActiveTabIndex + 1u) % Session->PresetRepository.size();
        Session->SelectPresetIdentity(Session->PresetRepository[NextIdx].PresetIdentity);
    }

    Surface.FillRectangle(RefBtnR, RefBtnR.Contains(Cursor) ? Theme.HoverSurface : Theme.SectionSurface, 6.0f);
    Surface.StrokeRectangle(RefBtnR, Theme.BorderSubtle, 6.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::RefreshCycle, VectorIconPlacement{ RefBtnR.X + 6.0f, RefBtnR.Y + 6.0f, 12.0f, Theme.TextMuted, 1.65f });

    Surface.FillRectangle(ExpBtnR, ExpBtnR.Contains(Cursor) ? Theme.HoverSurface : Theme.SectionSurface, 6.0f);
    Surface.StrokeRectangle(ExpBtnR, Theme.BorderSubtle, 6.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::DownloadExport, VectorIconPlacement{ ExpBtnR.X + 6.0f, ExpBtnR.Y + 6.0f, 12.0f, Theme.TextMuted, 1.65f });

    CurY += 28.0f;
    Surface.StrokeLine({ Bounds.X, CurY }, { Bounds.Right(), CurY }, Theme.BorderSubtle, 1.0f);
    CurY += 10.0f;

    // 2. Material Swatch + Title + Subtitle ("CERAMIC · IOR 1.50 · 6/28 CHANNELS ACTIVE", matching cards-material.png)
    const ColorRgba BaseCol = Outliner->ChannelTints[0];
    Surface.FillRectangle({ LeftX, CurY, 22.0f, 22.0f }, BaseCol, 6.0f);
    Surface.StrokeRectangle({ LeftX, CurY, 22.0f, 22.0f }, Theme.BorderStrong, 6.0f, 1.0f);

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Bold, { LeftX + 30.0f, CurY - 2.0f }, InnerW - 34.0f, 14.5f, Theme.TextPrimary, ActiveMatTitle);
    char SubMeta[80];
    std::snprintf(SubMeta, sizeof(SubMeta), "CERAMIC · IOR 1.50 · %zu/%zu CHANNELS ACTIVE", Outliner->CountEnabledChannels(), MaterialOutlinerPane::ChannelCount);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { LeftX + 30.0f, CurY + 14.0f }, InnerW - 34.0f, 7.8f, Theme.TextFaint, SubMeta);
    CurY += 32.0f;

    // 3. 4-Column Telemetry Strip (F0 | CLASS | GLOSS | ALBEDO, matching cards-material.png)
    const float TeleGap = 6.0f;
    const float TeleW   = (InnerW - TeleGap * 3.0f) / 4.0f;
    const float MetalVal = Outliner->ChannelScalars[2];
    const float RoughVal = Outliner->ChannelScalars[7];
    const int   GlossPct = static_cast<int>(std::lround((1.0f - RoughVal) * 100.0f));
    const int   AlbedoPct = static_cast<int>(std::lround(((BaseCol.Red + BaseCol.Green + BaseCol.Blue) / (3.0f * 255.0f)) * 100.0f));
    char GlossBuf[16], AlbedoBuf[16];
    std::snprintf(GlossBuf, sizeof(GlossBuf), "%d%%", GlossPct);
    std::snprintf(AlbedoBuf, sizeof(AlbedoBuf), "%d%%", AlbedoPct);

    struct TeleSpec { const char* Title; const char* Value; };
    const std::array<TeleSpec, 4u> TeleItems {{
        { "F0",     MetalVal > 0.5f ? "92.0%" : "4.0%"        },
        { "CLASS",  MetalVal > 0.5f ? "Conductor" : "Dielectric" },
        { "GLOSS",  GlossBuf                                  },
        { "ALBEDO", AlbedoBuf                                 },
    }};
    for (std::size_t Ti = 0u; Ti < 4u; ++Ti)
    {
        const Rectangle2D TileR{ LeftX + static_cast<float>(Ti) * (TeleW + TeleGap), CurY, TeleW, 38.0f };
        Surface.FillRectangle(TileR, Theme.SectionSurface, 8.0f);
        Surface.StrokeRectangle(TileR, Theme.BorderSubtle, 8.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold,     { TileR.X + 8.0f, TileR.Y + 5.0f  }, 7.2f, Theme.TextFaint,   TeleItems[Ti].Title);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { TileR.X + 8.0f, TileR.Y + 18.0f }, 9.6f, Theme.TextPrimary, TeleItems[Ti].Value);
    }
    CurY += 46.0f;

    // 4. Tiling & UV Collapsible Card ("Scale, offset, rotation & triplanar", matching cards-material.png)
    const Rectangle2D TilingHeadR{ LeftX, CurY, InnerW, 36.0f };
    Surface.FillRectangle(TilingHeadR, TilingHeadR.Contains(Cursor) ? Theme.HoverSurface : Theme.SectionSurface, 8.0f);
    Surface.StrokeRectangle(TilingHeadR, Theme.BorderSubtle, 8.0f, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { TilingHeadR.X + 10.0f, TilingHeadR.Y + 5.0f }, 9.4f, Theme.TextPrimary, "Tiling & UV");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,  { TilingHeadR.X + 10.0f, TilingHeadR.Y + 19.0f }, 7.8f, Theme.TextFaint,  "Scale, offset, rotation & triplanar");
    VectorIconRepository::ConstructControl(
        Surface, TilingSectionOpen ? ControlIcon::ChevronDown : ControlIcon::ChevronForward,
        VectorIconPlacement{ TilingHeadR.Right() - 18.0f, TilingHeadR.Y + 12.5f, 10.0f, Theme.TextMuted, 1.7f });
    if (Pressed && TilingHeadR.Contains(Cursor))
    {
        TilingSectionOpen = !TilingSectionOpen;
    }
    CurY += 42.0f;

    // 5. CHANNELS Header Row ("CHANNELS  6/28" + "Active only" toggle)
    char ChBadgeBuf[32];
    std::snprintf(ChBadgeBuf, sizeof(ChBadgeBuf), "%zu/%zu", Outliner->CountEnabledChannels(), MaterialOutlinerPane::ChannelCount);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold,   { LeftX, CurY + 3.0f }, 8.4f, Theme.TextFaint, "CHANNELS");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { LeftX + 58.0f, CurY + 3.0f }, 8.2f, Theme.TextMuted, ChBadgeBuf);

    const Rectangle2D ActOnlyBtnR{ Bounds.Right() - PadX - 78.0f, CurY, 78.0f, 20.0f };
    Surface.FillRectangle(ActOnlyBtnR, ActiveOnlyFilter ? ColorRgba{ 238u, 238u, 236u, 255u } : Theme.SectionSurface, 6.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { ActOnlyBtnR.X + 10.0f, ActOnlyBtnR.Y + 3.5f }, 7.8f,
        ActiveOnlyFilter ? ColorRgba{ 16u, 17u, 20u, 255u } : Theme.TextMuted, "Active only");
    if (Pressed && ActOnlyBtnR.Contains(Cursor))
    {
        ActiveOnlyFilter = !ActiveOnlyFilter;
    }
    CurY += 26.0f;
    Surface.StrokeLine({ Bounds.X, CurY }, { Bounds.Right(), CurY }, Theme.BorderSubtle, 1.0f);
    CurY += 4.0f;

    // 6. Scrollable 6-Group ChannelStack with Side-by-Side Open Channel Cards (cards-material.png parity)
    constexpr float FooterH = 26.0f;
    const Rectangle2D ScrollRect{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - FooterH - CurY)
    };
    if (ScrollRect.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetY = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScrollY);
    }

    Surface.PushClip(ScrollRect);
    float Sy = ScrollRect.Y + 6.0f - ScrollOffsetY;
    const float ScrollStartY = Sy;

    const auto& Catalog    = MaterialOutlinerPane::QueryChannelCatalog();
    const auto& GroupNames = MaterialOutlinerPane::QueryGroupNames();

    // Render Side-by-Side Open Channels Showcase Row at the top of the stack (Colour + Roughness side-by-side, matching cards-material.png)
    std::vector<std::size_t> OpenChannelIndices;
    for (std::size_t C = 0u; C < MaterialOutlinerPane::ChannelCount; ++C)
    {
        if (Outliner->ChannelExpanded[C] && (!ActiveOnlyFilter || Outliner->ChannelEnabled[C]))
        {
            OpenChannelIndices.push_back(C);
        }
    }

    if (!OpenChannelIndices.empty())
    {
        const float ColGap = 8.0f;
        const float ColW   = (InnerW - ColGap) * 0.5f;
        const float CardH  = 114.0f;
        const std::size_t PairRows = (OpenChannelIndices.size() + 1u) / 2u;

        for (std::size_t K = 0u; K < OpenChannelIndices.size(); ++K)
        {
            const std::size_t CIdx = OpenChannelIndices[K];
            const auto& Spec       = Catalog[CIdx];
            const std::size_t Col  = K % 2u;
            const std::size_t Row  = K / 2u;
            const bool SingleLast  = (Col == 0u && K + 1u == OpenChannelIndices.size());
            const float CardW      = SingleLast ? InnerW : ColW;

            const Rectangle2D CardR{
                LeftX + static_cast<float>(Col) * (ColW + ColGap),
                Sy + static_cast<float>(Row) * (CardH + 8.0f),
                CardW,
                CardH
            };

            // Subtle neutral border only — zero amber border!
            Surface.FillRectangle(CardR, Theme.WellSurface, 10.0f);
            Surface.StrokeRectangle(CardR, Theme.BorderSubtle, 10.0f, 1.0f);

            // Header inside open channel card
            Surface.FillRectangle({ CardR.X + 8.0f, CardR.Y + 8.0f, 14.0f, 14.0f },
                Spec.ColorChannel ? Outliner->ChannelTints[CIdx] : Spec.DefaultTint, 4.0f);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::SemiBold, { CardR.X + 26.0f, CardR.Y + 7.5f }, CardW - 76.0f, 9.4f, Theme.TextPrimary, Spec.Label);

            char ReadoutBuf[24];
            if (Spec.ColorChannel)
            {
                std::snprintf(ReadoutBuf, sizeof(ReadoutBuf), "#%02x%02x%02x",
                    Outliner->ChannelTints[CIdx].Red, Outliner->ChannelTints[CIdx].Green, Outliner->ChannelTints[CIdx].Blue);
            }
            else
            {
                std::snprintf(ReadoutBuf, sizeof(ReadoutBuf), "%d %%", static_cast<int>(std::lround(Outliner->ChannelScalars[CIdx] * 100.0f)));
            }
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { CardR.Right() - 48.0f, CardR.Y + 8.0f }, 8.0f, Theme.TextMuted, ReadoutBuf);

            // 4-tab strip: "Value | Field | Ramp | Image" (matching cards-material.png)
            static constexpr const char* SourceTabs[4] { "Value", "Field", "Ramp", "Image" };
            const float TabW = (CardW - 16.0f - 6.0f) / 4.0f;
            for (uint32_t Tm = 0u; Tm < 4u; ++Tm)
            {
                const Rectangle2D TabR{ CardR.X + 8.0f + static_cast<float>(Tm) * (TabW + 2.0f), CardR.Y + 28.0f, TabW, 20.0f };
                const bool TabAct = (Outliner->ChannelSourceTab[CIdx] == Tm);
                Surface.FillRectangle(TabR, TabAct ? ColorRgba{ 238u, 238u, 236u, 255u } : Theme.SectionSurface, 5.0f);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, TabAct ? FontWeightClass::SemiBold : FontWeightClass::Medium, { TabR.X + 5.0f, TabR.Y + 3.5f }, TabW - 8.0f, 7.8f,
                    TabAct ? ColorRgba{ 16u, 17u, 20u, 255u } : Theme.TextMuted, SourceTabs[Tm]);
                if (Pressed && ScrollRect.Contains(Cursor) && TabR.Contains(Cursor))
                {
                    Outliner->ChannelSourceTab[CIdx] = static_cast<uint8_t>(Tm);
                }
            }

            // Control inside open channel card
            if (Spec.ColorChannel)
            {
                const Rectangle2D SwBarR{ CardR.X + 8.0f, CardR.Y + 56.0f, CardW - 16.0f, 28.0f };
                Surface.FillRectangle(SwBarR, Theme.SectionSurface, 7.0f);
                Surface.StrokeRectangle(SwBarR, Theme.BorderSubtle, 7.0f, 1.0f);
                Surface.FillRectangle({ SwBarR.X + 4.0f, SwBarR.Y + 4.0f, 24.0f, 20.0f }, Outliner->ChannelTints[CIdx], 5.0f);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { SwBarR.X + 34.0f, SwBarR.Y + 7.0f }, 8.8f, Theme.TextPrimary, ReadoutBuf);

                // Quick swatch dots
                constexpr std::array<ColorRgba, 5u> QuickDots {{
                    { 185u, 122u,  69u, 255u },
                    { 232u, 194u, 122u, 255u },
                    {  92u, 148u, 188u, 255u },
                    { 112u, 158u, 126u, 255u },
                    { 224u, 226u, 230u, 255u },
                }};
                for (std::size_t Qd = 0u; Qd < QuickDots.size(); ++Qd)
                {
                    const Point2D DotC{ CardR.X + 16.0f + static_cast<float>(Qd) * 20.0f, CardR.Y + 96.0f };
                    Surface.FillCircle(DotC, 6.0f, QuickDots[Qd]);
                    if (Pressed && ScrollRect.Contains(Cursor))
                    {
                        const float Dx = Cursor.X - DotC.X;
                        const float Dy = Cursor.Y - DotC.Y;
                        if (Dx * Dx + Dy * Dy <= 49.0f)
                        {
                            Outliner->ChannelTints[CIdx] = QuickDots[Qd];
                        }
                    }
                }
            }
            else
            {
                const int32_t Sid = 400 + static_cast<int32_t>(CIdx);
                bool Drag = (ActiveSliderId == Sid);
                float Norm = std::clamp(Outliner->ChannelScalars[CIdx], 0.0f, 1.0f);
                char NumStr[24];
                std::snprintf(NumStr, sizeof(NumStr), "%d", static_cast<int>(std::lround(Norm * 100.0f)));
                if (ConstructSliderReadoutRow(
                        Surface, CardR.X + 8.0f, CardR.Y + 56.0f, CardW - 16.0f, Spec.Label, NumStr, Spec.UnitSuffix, Norm, Theme, Input.PrimaryPointer, Drag))
                {
                    Outliner->ChannelScalars[CIdx] = Norm;
                }
                if (Drag) ActiveSliderId = Sid;
            }
        }
        Sy += static_cast<float>(PairRows) * (CardH + 8.0f) + 4.0f;
    }

    // Collapsible 6-Group Channel Stack
    for (std::size_t G = 0u; G < MaterialOutlinerPane::GroupCount; ++G)
    {
        const MaterialChannelGroup GroupEnum = static_cast<MaterialChannelGroup>(G);
        std::size_t GroupTotal = 0u;
        std::size_t GroupOn    = 0u;
        for (std::size_t C = 0u; C < MaterialOutlinerPane::ChannelCount; ++C)
        {
            if (Catalog[C].Group == GroupEnum)
            {
                ++GroupTotal;
                if (Outliner->ChannelEnabled[C]) ++GroupOn;
            }
        }
        if (ActiveOnlyFilter && GroupOn == 0u) continue;

        const Rectangle2D GHeadR{ LeftX, Sy, InnerW, 26.0f };
        const bool GOpen = Outliner->GroupOpen[G];
        Surface.FillRectangle(GHeadR, GHeadR.Contains(Cursor) ? Theme.HoverSurface : Theme.SectionSurface, 6.0f);
        VectorIconRepository::ConstructControl(
            Surface, GOpen ? ControlIcon::ChevronDown : ControlIcon::ChevronForward,
            VectorIconPlacement{ GHeadR.X + 8.0f, GHeadR.Y + 8.0f, 10.0f, Theme.TextMuted, 1.7f });
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { GHeadR.X + 24.0f, GHeadR.Y + 6.0f }, 8.6f, Theme.TextPrimary, GroupNames[G]);

        char GCntBuf[24];
        std::snprintf(GCntBuf, sizeof(GCntBuf), "%zu/%zu on", GroupOn, GroupTotal);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { GHeadR.Right() - 48.0f, GHeadR.Y + 6.5f }, 8.0f, Theme.TextFaint, GCntBuf);

        if (Pressed && ScrollRect.Contains(Cursor) && GHeadR.Contains(Cursor))
        {
            Outliner->GroupOpen[G] = !Outliner->GroupOpen[G];
        }
        Sy += 29.0f;

        if (!GOpen) continue;

        for (std::size_t C = 0u; C < MaterialOutlinerPane::ChannelCount; ++C)
        {
            const auto& Spec = Catalog[C];
            if (Spec.Group != GroupEnum) continue;
            if (ActiveOnlyFilter && !Outliner->ChannelEnabled[C]) continue;

            const Rectangle2D RowR{ LeftX, Sy, InnerW, 30.0f };
            const bool Hov = ScrollRect.Contains(Cursor) && RowR.Contains(Cursor);
            Surface.FillRectangle(RowR, Outliner->ChannelExpanded[C] ? Theme.HoverSurface : (Hov ? Theme.SectionSurface : Theme.WellSurface), 7.0f);
            Surface.StrokeRectangle(RowR, Theme.BorderSubtle, 7.0f, 1.0f);

            Surface.FillRectangle({ RowR.X + 8.0f, RowR.Y + 8.0f, 14.0f, 14.0f },
                Spec.ColorChannel ? Outliner->ChannelTints[C] : Spec.DefaultTint, 4.0f);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, Outliner->ChannelEnabled[C] ? FontWeightClass::SemiBold : FontWeightClass::Regular,
                { RowR.X + 28.0f, RowR.Y + 7.5f }, InnerW - 110.0f, 9.0f,
                Outliner->ChannelEnabled[C] ? Theme.TextPrimary : Theme.TextMuted, Spec.Label);

            char ValBuf[24];
            if (!Outliner->ChannelEnabled[C]) std::snprintf(ValBuf, sizeof(ValBuf), "Off");
            else if (Spec.ColorChannel) std::snprintf(ValBuf, sizeof(ValBuf), "#%02x%02x%02x", Outliner->ChannelTints[C].Red, Outliner->ChannelTints[C].Green, Outliner->ChannelTints[C].Blue);
            else std::snprintf(ValBuf, sizeof(ValBuf), "%d %%", static_cast<int>(std::lround(Outliner->ChannelScalars[C] * 100.0f)));

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { RowR.Right() - 68.0f, RowR.Y + 8.0f }, 8.0f, Theme.TextMuted, ValBuf);

            const Rectangle2D ToggleR{ RowR.Right() - 28.0f, RowR.Y + 8.0f, 22.0f, 14.0f };
            Surface.FillRectangle(ToggleR, Outliner->ChannelEnabled[C] ? ColorRgba{ 238u, 238u, 236u, 255u } : Theme.SectionSurface, 7.0f);
            Surface.FillCircle(
                { Outliner->ChannelEnabled[C] ? (ToggleR.Right() - 7.0f) : (ToggleR.X + 7.0f), ToggleR.Y + 7.0f },
                4.8f,
                Outliner->ChannelEnabled[C] ? ColorRgba{ 16u, 17u, 20u, 255u } : Theme.TextFaint);

            if (Pressed && Hov)
            {
                if (ToggleR.Contains(Cursor))
                {
                    Outliner->ChannelEnabled[C] = !Outliner->ChannelEnabled[C];
                }
                else
                {
                    Outliner->ChannelExpanded[C] = !Outliner->ChannelExpanded[C];
                }
            }
            Sy += 33.0f;
        }
        Sy += 4.0f;
    }

    MaxScrollY = std::max(0.0f, (Sy - ScrollStartY) - ScrollRect.Height + 12.0f);
    Surface.PopClip();

    // 7. Bottom Status Footer
    const Rectangle2D FooterR{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    char StatusLeft[48];
    std::snprintf(StatusLeft, sizeof(StatusLeft), "%zu / %zu channels active", Outliner->CountEnabledChannels(), MaterialOutlinerPane::ChannelCount);
    ConstructPaneStatusFooter(Surface, FooterR, StatusLeft, "OpenPBR 1.1 · Linear sRGB", Theme, false);

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
