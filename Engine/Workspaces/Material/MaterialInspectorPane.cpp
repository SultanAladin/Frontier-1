/*============================================================================================================================================
                                                        MATERIALINSPECTORPANE.CPP
============================================================================================================================================*/
// 🧩 Implementation of MaterialInspectorPane: 1:1 parity with Experimental/TextureStudio docked Material Inspector (cards-material.png
//    & outliner-footer-material.png): LOOK-DEV PBR header, 3 KPI metric tiles (Response, Albedo, Signal), 28-channel stack with 28x28
//    procedural icons, in-place vertical multi-open channel cards (Value | Field | Ramp | Image), and collapsed group mini swatches.

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
    Surface.FillRectangle(Bounds, Theme.LightSurface ? Theme.PaneBackground : ColorRgba{ 24u, 25u, 28u, 255u }, 0.0f);

    const float PadX   = 14.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float       CurY   = Bounds.Y + 12.0f;

    // 1. Top Card Bar ("Material Inspector / LOOK-DEV PBR" + Undo Redo Upload Reset)
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::MaterialSphere, VectorIconPlacement{ LeftX, CurY + 3.0f, 14.0f, ColorRgba{ 242u, 140u, 56u, 255u }, 1.75f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { LeftX + 20.0f, CurY - 1.0f }, 10.2f, Theme.TextPrimary, "Material Inspector");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { LeftX + 20.0f, CurY + 11.5f }, 7.2f, Theme.TextFaint, "LOOK-DEV PBR");

    static constexpr std::array<ControlIcon, 4u> TopActions {{
        ControlIcon::UndoArrow,
        ControlIcon::RedoArrow,
        ControlIcon::UploadImport,
        ControlIcon::RefreshCycle,
    }};
    for (std::size_t I = 0u; I < TopActions.size(); ++I)
    {
        const Rectangle2D ActR{
            Bounds.Right() - PadX - static_cast<float>(TopActions.size() - I) * 24.0f,
            CurY,
            22.0f,
            22.0f
        };
        ConstructHeaderIconButton(Surface, ActR, TopActions[I], false, I >= 2u, Theme, Cursor, Pressed);
    }
    CurY += 28.0f;
    Surface.StrokeLine({ LeftX, CurY }, { LeftX + InnerW, CurY }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
    CurY += 10.0f;

    // 2. Hero Title Block ("ANIMATED & VFX / 01", Large Title, Subtitle + Preset Dropdown Pill)
    const char* ActiveMatTitle = (Session != nullptr && !Session->DocumentTabs.empty())
        ? Session->InspectActiveRecipe().DisplayTitle.c_str()
        : "Derez Lattice Dissolve (ControlVehicle)";

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { LeftX, CurY }, 7.6f, Theme.TextFaint, "ANIMATED & VFX / 01");
    CurY += 13.0f;

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, CurY }, InnerW, 17.5f, Theme.TextPrimary, ActiveMatTitle);
    CurY += 24.0f;

    const float DropW = std::min(156.0f, InnerW * 0.48f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { LeftX, CurY + 5.0f }, InnerW - DropW - 8.0f, 7.6f, Theme.TextFaint,
        "ANIMATED 16² LATTICE · HMI DEREZ");

    const Rectangle2D PresetR{ LeftX + InnerW - DropW, CurY, DropW, 24.0f };
    Surface.FillRectangle(PresetR, PresetR.Contains(Cursor) ? ColorRgba{ 20u, 21u, 24u, 255u } : ColorRgba{ 10u, 11u, 13u, 255u }, 12.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { PresetR.X + 10.0f, PresetR.Y + 5.0f }, PresetR.Width - 24.0f, 8.2f, Theme.TextPrimary, ActiveMatTitle);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ChevronDown, VectorIconPlacement{ PresetR.Right() - 14.0f, PresetR.Y + 7.5f, 9.0f, Theme.TextMuted, 1.6f });
    if (Pressed && PresetR.Contains(Cursor) && Session != nullptr && !Session->PresetRepository.empty())
    {
        const std::size_t NextIdx = (Session->ActiveTabIndex + 1u) % Session->PresetRepository.size();
        Session->SelectPresetIdentity(Session->PresetRepository[NextIdx].PresetIdentity);
    }
    CurY += 32.0f;

    // 3. Three KPI Metric Tiles: Response (0.028 F0), Albedo (5 %), Signal (0 / 28) — matches cards-material.png 1:1
    const float TileGap = 8.0f;
    const float TileW   = (InnerW - TileGap * 2.0f) / 3.0f;
    const float TileH   = 66.0f;
    struct MetricSpec
    {
        const char* Label;
        const char* BigNum;
        const char* Unit;
        float       BarFrac;
        ColorRgba   BarCol;
        const char* SubText;
    };
    const std::array<MetricSpec, 3u> Metrics {{
        { "Response", "0.028", "F0",   0.18f, { 235u, 235u, 235u, 255u }, "HYBRID · MIRROR" },
        { "Albedo",   "5",     "%",    0.08f, { 180u, 182u, 188u, 255u }, "TOO DARK"        },
        { "Signal",   "0",     "/ 28", 0.02f, { 120u, 122u, 128u, 255u }, "ALL FLAT VALUES" },
    }};
    for (std::size_t Mi = 0u; Mi < 3u; ++Mi)
    {
        const Rectangle2D TileR{ LeftX + static_cast<float>(Mi) * (TileW + TileGap), CurY, TileW, TileH };
        Surface.FillRectangle(TileR, ColorRgba{ 33u, 34u, 39u, 255u }, 12.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { TileR.X + 10.0f, TileR.Y + 7.0f }, 8.2f, Theme.TextMuted, Metrics[Mi].Label);
        const float BigW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 15.0f, Metrics[Mi].BigNum);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { TileR.X + 10.0f, TileR.Y + 20.0f }, 15.0f, Theme.TextPrimary, Metrics[Mi].BigNum);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { TileR.X + 13.0f + BigW, TileR.Y + 25.0f }, 9.2f, Theme.TextMuted, Metrics[Mi].Unit);

        const Rectangle2D BarTrack{ TileR.X + 10.0f, TileR.Y + 42.0f, TileR.Width - 20.0f, 3.5f };
        Surface.FillRectangle(BarTrack, ColorRgba{ 19u, 20u, 23u, 255u }, 1.8f);
        const float FillW = std::max(3.0f, BarTrack.Width * Metrics[Mi].BarFrac);
        Surface.FillRectangle({ BarTrack.X, BarTrack.Y, FillW, BarTrack.Height }, Metrics[Mi].BarCol, 1.8f);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { TileR.X + 10.0f, TileR.Y + 50.0f }, TileR.Width - 16.0f, 7.0f, Theme.TextFaint, Metrics[Mi].SubText);
    }
    CurY += TileH + 12.0f;

    // 4. Channels Section Header ("Channels  28 ON SURFACE" + "+ Channel" button + "Filter 28 channels..." search pill)
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, CurY + 2.0f }, 10.2f, Theme.TextPrimary, "Channels");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { LeftX + 54.0f, CurY + 4.0f }, 7.6f, Theme.TextFaint, "28 ON SURFACE");

    const Rectangle2D AddChBtnR{ LeftX + InnerW - 72.0f, CurY, 72.0f, 22.0f };
    Surface.FillRectangle(AddChBtnR, AddChBtnR.Contains(Cursor) ? ColorRgba{ 42u, 44u, 50u, 255u } : ColorRgba{ 33u, 34u, 39u, 255u }, 11.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { AddChBtnR.X + 11.0f, AddChBtnR.Y + 4.0f }, 8.4f, Theme.TextPrimary, "+ Channel");
    CurY += 28.0f;

    const Rectangle2D SearchR{ LeftX, CurY, InnerW, 28.0f };
    Surface.FillRectangle(SearchR, ColorRgba{ 19u, 20u, 23u, 255u }, 14.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchR.X + 10.0f, SearchR.Y + 8.0f, 12.0f, Theme.TextFaint, 1.6f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { SearchR.X + 28.0f, SearchR.Y + 6.5f }, 9.0f, Theme.TextFaint, "Filter 28 channels...");
    CurY += 34.0f;

    // 5. Scrollable 6-Group Vertical Channel Stack (in-place multi-open editors, zero 2-column grid!)
    const Rectangle2D ScrollRect{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - CurY)
    };
    if (ScrollRect.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetY = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScrollY);
    }

    Surface.PushClip(ScrollRect);
    float Sy = ScrollRect.Y + 2.0f - ScrollOffsetY;
    const float ScrollStartY = Sy;

    const auto& Catalog         = MaterialOutlinerPane::QueryChannelCatalog();
    const auto& GroupUpperNames = MaterialOutlinerPane::QueryGroupUpperNames();

    for (std::size_t G = 0u; G < MaterialOutlinerPane::GroupCount; ++G)
    {
        const MaterialChannelGroup GroupEnum = static_cast<MaterialChannelGroup>(G);
        std::size_t               GroupTotal = 0u;
        std::array<ColorRgba, 6u> MiniColors {};
        std::array<bool, 6u>      MiniIsCol  {};
        std::array<float, 6u>     MiniScalar {};
        std::size_t               MiniCount  = 0u;
        for (std::size_t C = 0u; C < MaterialOutlinerPane::ChannelCount; ++C)
        {
            if (Catalog[C].Group == GroupEnum)
            {
                ++GroupTotal;
                if (MiniCount < 6u)
                {
                    MiniColors[MiniCount] = Outliner->ChannelTints[C];
                    MiniIsCol[MiniCount]  = Catalog[C].ColorChannel;
                    MiniScalar[MiniCount] = Outliner->ChannelScalars[C];
                    ++MiniCount;
                }
            }
        }

        const Rectangle2D GHeadR{ LeftX, Sy, InnerW, 26.0f };
        const bool GOpen = Outliner->InspectorGroupOpen[G];
        if (GHeadR.Contains(Cursor))
        {
            Surface.FillRectangle(GHeadR, ColorRgba{ 30u, 31u, 36u, 255u }, 6.0f);
        }
        VectorIconRepository::ConstructControl(
            Surface, GOpen ? ControlIcon::ChevronDown : ControlIcon::ChevronForward,
            VectorIconPlacement{ GHeadR.X + 4.0f, GHeadR.Y + 8.0f, 9.0f, Theme.TextMuted, 1.7f });
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { GHeadR.X + 18.0f, GHeadR.Y + 5.5f }, 8.2f, Theme.TextMuted, GroupUpperNames[G]);

        char CntBuf[16];
        std::snprintf(CntBuf, sizeof(CntBuf), "%zu", GroupTotal);
        const float NameW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.2f, GroupUpperNames[G]);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { GHeadR.X + 24.0f + NameW, GHeadR.Y + 5.5f }, 8.0f, Theme.TextFaint, CntBuf);

        if (!GOpen && GroupTotal > 0u)
        {
            ConstructCollapsedGroupMiniSwatches(
                Surface,
                GHeadR.Right() - 4.0f,
                GHeadR.Y + 13.0f,
                static_cast<uint32_t>(GroupTotal));
        }
        if (Pressed && GHeadR.Contains(Cursor))
        {
            Outliner->InspectorGroupOpen[G] = !Outliner->InspectorGroupOpen[G];
        }
        Sy += 28.0f;

        if (!GOpen)
        {
            continue;
        }

        for (std::size_t C = 0u; C < MaterialOutlinerPane::ChannelCount; ++C)
        {
            const auto& Spec = Catalog[C];
            if (Spec.Group != GroupEnum) continue;

            char ReadoutBuf[24];
            if (Spec.ColorChannel)
            {
                FormatHexLower(Outliner->ChannelTints[C], ReadoutBuf, sizeof(ReadoutBuf));
                for (char* P = ReadoutBuf; *P; ++P)
                {
                    if (*P >= 'a' && *P <= 'f') *P = static_cast<char>(*P - 'a' + 'A');
                }
            }
            else if (Spec.NormalChannel)
            {
                std::snprintf(ReadoutBuf, sizeof(ReadoutBuf), "Flat +Z");
            }
            else
            {
                std::snprintf(ReadoutBuf, sizeof(ReadoutBuf), "%.2f", Outliner->ChannelScalars[C]);
            }

            const bool IsOpen = Outliner->ChannelExpanded[C];
            if (!IsOpen)
            {
                const Rectangle2D RowR{ LeftX, Sy, InnerW, 38.0f };
                if (RowR.Contains(Cursor))
                {
                    Surface.FillRectangle(RowR, ColorRgba{ 29u, 30u, 34u, 255u }, 8.0f);
                }
                const Rectangle2D SwatchR{ RowR.X + 6.0f, RowR.Y + 7.0f, 24.0f, 24.0f };
                ConstructProceduralChannelIcon(
                    Surface, SwatchR, Spec.Label, Outliner->ChannelTints[C], Spec.ColorChannel, Outliner->ChannelScalars[C], true);

                const float PillW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.6f, ReadoutBuf) + 18.0f;
                const Rectangle2D PillR{ RowR.Right() - PillW - 4.0f, RowR.Y + 9.0f, PillW, 20.0f };
                Surface.FillRectangle(PillR, ColorRgba{ 9u, 10u, 12u, 255u }, 10.0f);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { PillR.X + 9.0f, PillR.Y + 3.5f }, 8.6f, Theme.TextPrimary, ReadoutBuf);

                const float TextX = SwatchR.Right() + 10.0f;
                const float TextW = std::max(32.0f, PillR.X - TextX - 8.0f);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Medium, { TextX, RowR.Y + 5.5f }, TextW, 10.0f, Theme.TextPrimary, Spec.Label);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Regular, { TextX, RowR.Y + 20.0f }, TextW, 8.2f, Theme.TextFaint, Spec.Subtitle);

                if (Pressed && RowR.Contains(Cursor))
                {
                    Outliner->ChannelExpanded[C]  = true;
                    Outliner->SelectedChannelIndex = static_cast<uint32_t>(C);
                }
                Sy += 40.0f;
            }
            else
            {
                // Open channel card in-place vertically (matches cards-material.png & outliner-footer-material.png 1:1)
                const float CardH = Spec.ColorChannel ? 132.0f : 148.0f;
                const Rectangle2D CardR{ LeftX, Sy, InnerW, CardH };
                Surface.FillRectangle(CardR, ColorRgba{ 34u, 35u, 39u, 255u }, 12.0f);

                const Rectangle2D HeadR{ CardR.X, CardR.Y, CardR.Width, 38.0f };
                const Rectangle2D SwatchR{ CardR.X + 10.0f, CardR.Y + 8.0f, 24.0f, 24.0f };
                ConstructProceduralChannelIcon(
                    Surface, SwatchR, Spec.Label, Outliner->ChannelTints[C], Spec.ColorChannel, Outliner->ChannelScalars[C], true);

                const float PillW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.6f, ReadoutBuf) + 18.0f;
                const Rectangle2D PillR{ CardR.Right() - PillW - 10.0f, CardR.Y + 10.0f, PillW, 20.0f };
                Surface.FillRectangle(PillR, ColorRgba{ 9u, 10u, 12u, 255u }, 10.0f);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { PillR.X + 9.0f, PillR.Y + 3.5f }, 8.6f, Theme.TextPrimary, ReadoutBuf);

                const float TextX = SwatchR.Right() + 10.0f;
                const float TextW = std::max(32.0f, PillR.X - TextX - 8.0f);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Medium, { TextX, CardR.Y + 6.5f }, TextW, 10.0f, Theme.TextPrimary, Spec.Label);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Regular, { TextX, CardR.Y + 21.0f }, TextW, 8.2f, Theme.TextFaint, Spec.Subtitle);

                if (Pressed && HeadR.Contains(Cursor))
                {
                    Outliner->ChannelExpanded[C] = false;
                }

                // Segmented source bar: Value | Field | Ramp | Image
                const Rectangle2D SegBarR{ CardR.X + 10.0f, CardR.Y + 42.0f, CardR.Width - 20.0f, 24.0f };
                Surface.FillRectangle(SegBarR, ColorRgba{ 18u, 18u, 20u, 255u }, 12.0f);
                static constexpr const char* TabLabels[4] { "Value", "Field", "Ramp", "Image" };
                const float TabW = (SegBarR.Width - 6.0f) / 4.0f;
                for (uint8_t Ti = 0u; Ti < 4u; ++Ti)
                {
                    const Rectangle2D TabR{ SegBarR.X + 3.0f + static_cast<float>(Ti) * TabW, SegBarR.Y + 2.0f, TabW, 20.0f };
                    const bool TabAct = (Outliner->ChannelSourceTab[C] == Ti);
                    if (TabAct)
                    {
                        Surface.FillRectangle(TabR, ColorRgba{ 255u, 255u, 255u, 255u }, 10.0f);
                        Surface.FillCircle({ TabR.X + 10.0f, TabR.Y + 10.0f }, 2.2f, ColorRgba{ 17u, 17u, 17u, 255u });
                    }
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, TabAct ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                        { TabR.X + (TabAct ? 16.0f : (TabW - 24.0f) * 0.5f), TabR.Y + 3.5f }, 8.2f,
                        TabAct ? ColorRgba{ 17u, 17u, 17u, 255u } : Theme.TextMuted, TabLabels[Ti]);
                    if (Pressed && TabR.Contains(Cursor))
                    {
                        Outliner->ChannelSourceTab[C] = Ti;
                    }
                }

                if (Spec.ColorChannel)
                {
                    const Rectangle2D ColBarR{ CardR.X + 10.0f, CardR.Y + 73.0f, CardR.Width - 20.0f, 28.0f };
                    Surface.FillRectangle(ColBarR, ColorRgba{ 9u, 10u, 12u, 255u }, 14.0f);
                    Surface.FillRectangle({ ColBarR.X + 5.0f, ColBarR.Y + 4.0f, 20.0f, 20.0f }, Outliner->ChannelTints[C], 10.0f);
                    Surface.StrokeRectangle({ ColBarR.X + 5.0f, ColBarR.Y + 4.0f, 20.0f, 20.0f }, ColorRgba{ 255u, 255u, 255u, 36u }, 10.0f, 1.0f);
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Medium, { ColBarR.X + 32.0f, ColBarR.Y + 7.5f }, 9.0f, Theme.TextPrimary, ReadoutBuf);
                    WorkspaceAppearance::ConstructRightAlignedText(
                        Surface, FontWeightClass::Regular, { ColBarR.Right() - 12.0f, ColBarR.Y + 8.0f }, 8.2f, Theme.TextFaint, "sRGB");
                }
                else
                {
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Regular, { CardR.X + 10.0f, CardR.Y + 70.0f }, 8.4f, Theme.TextMuted, Spec.Label);
                    const Rectangle2D ValBoxR{ CardR.X + 10.0f, CardR.Y + 86.0f, 54.0f, 24.0f };
                    Surface.FillRectangle(ValBoxR, ColorRgba{ 9u, 10u, 12u, 255u }, 12.0f);
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Medium, { ValBoxR.X + 11.0f, ValBoxR.Y + 5.5f }, 8.8f, Theme.TextPrimary, ReadoutBuf);

                    const Rectangle2D TrackR{ ValBoxR.Right() + 8.0f, CardR.Y + 88.0f, CardR.Right() - 10.0f - (ValBoxR.Right() + 8.0f), 20.0f };
                    Surface.FillRectangle(TrackR, ColorRgba{ 23u, 24u, 27u, 255u }, 10.0f);
                    const float Norm  = std::clamp(Outliner->ChannelScalars[C], 0.0f, 1.0f);
                    const float FillW = std::max(10.0f, TrackR.Width * Norm);
                    Surface.FillRectangle({ TrackR.X, TrackR.Y, FillW, TrackR.Height }, ColorRgba{ 76u, 77u, 82u, 255u }, 10.0f);
                    const float ThumbX = std::clamp(TrackR.X + TrackR.Width * Norm, TrackR.X + 9.0f, TrackR.Right() - 9.0f);
                    Surface.FillCircle({ ThumbX, TrackR.Y + 10.0f }, 7.5f, ColorRgba{ 235u, 235u, 235u, 255u });

                    const int32_t SliderId = 1000 + static_cast<int32_t>(C);
                    if (Pressed && TrackR.Contains(Cursor))
                    {
                        ActiveSliderId = SliderId;
                    }
                    if (ActiveSliderId == SliderId && Input.PrimaryPointer.Down)
                    {
                        Outliner->ChannelScalars[C] = std::clamp((Cursor.X - TrackR.X) / std::max(1.0f, TrackR.Width), 0.0f, 1.0f);
                    }
                }

                // Bottom footer row inside open channel card: slug on left + "× Remove" button on right
                const float FootY = CardR.Bottom() - 22.0f;
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Regular, { CardR.X + 12.0f, FootY + 2.0f }, 8.0f, Theme.TextFaint, Spec.KeySlug);
                const Rectangle2D RemBtnR{ CardR.Right() - 68.0f, FootY - 1.0f, 58.0f, 18.0f };
                Surface.FillRectangle(RemBtnR, ColorRgba{ 24u, 25u, 28u, 255u }, 9.0f);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { RemBtnR.X + 9.0f, RemBtnR.Y + 2.5f }, 7.8f, Theme.TextMuted, "x Remove");

                Sy += CardH + 6.0f;
            }
        }
        Sy += 4.0f;
    }

    const float ContentH = (Sy - ScrollStartY) + 12.0f;
    MaxScrollY = std::max(0.0f, ContentH - ScrollRect.Height);
    Surface.PopClip();
    Surface.PopClip();
}

} // namespace Frontier::Workspaces
