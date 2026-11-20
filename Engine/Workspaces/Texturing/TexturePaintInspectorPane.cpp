/*==========================================================================================================================================
                                                   TEXTUREPAINTINSPECTORPANE.CPP
==========================================================================================================================================*/
// 🎛️ Implementation of TexturePaintInspectorPane: Project Zero PaintInspector & ChannelStack parity with gradient points & inline channel editors.

#include "Engine/Workspaces/Texturing/TexturePaintInspectorPane.h"

#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Workspaces/Texturing/TexturingPaneAppearance.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <string>

namespace Frontier::Workspaces
{

namespace
{

using namespace TexturingAppearance;

[[nodiscard]] const char* QueryCategoryUpperTag(PaintLayerCategory Category) noexcept
{
    switch (Category)
    {
        case PaintLayerCategory::Fill:     return "FILL LAYER";
        case PaintLayerCategory::Material: return "MATERIAL LAYER";
        case PaintLayerCategory::Stamp:    return "STAMP LAYER";
        case PaintLayerCategory::Text:     return "TEXT LAYER";
        case PaintLayerCategory::WearGen:  return "GENERATOR LAYER";
        case PaintLayerCategory::Gradient: return "GRADIENT LAYER";
        case PaintLayerCategory::Folder:   return "LAYER GROUP";
        case PaintLayerCategory::Paint:
        default:                           return "PAINT LAYER";
    }
}

[[nodiscard]] bool ChannelMatchesQuery(const PaintChannelDescriptor& Desc, std::string_view Query) noexcept
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
    return ContainsCaseInsensitive(Desc.Label, Query) || ContainsCaseInsensitive(Desc.SubHint, Query);
}

} // namespace

TexturePaintInspectorPane::~TexturePaintInspectorPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void TexturePaintInspectorPane::AttachSession(TexturePaintSession* TargetSession) noexcept
{
    Session = TargetSession;
}

void TexturePaintInspectorPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##TexturePaintInspectorCanvas",
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

void TexturePaintInspectorPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  /*DeltaSeconds*/)
{
    if (Session == nullptr || Bounds.Width < 60.0f || Bounds.Height < 60.0f)
    {
        return;
    }

    const TexturingThemeColors Theme = QueryTexturingTheme(Surface);
    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground);
    Surface.StrokeLine({ Bounds.X + 0.5f, Bounds.Y }, { Bounds.X + 0.5f, Bounds.Bottom() }, Theme.BorderSubtle, 1.0f);

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    const float   PadX    = 14.0f;
    const float   InnerW  = std::max(140.0f, Bounds.Width - PadX * 2.0f);
    float         CurY    = Bounds.Y + 12.0f;

    //======================================================================================================================
    // 1. HEADER ("Layer Inspector", "TEXTURE PAINT · UTAH TEAPOT", Undo / Redo / Import / Export / Reset)
    //======================================================================================================================
    const Rectangle2D HeadIconBox{ Bounds.X + PadX, CurY + 1.0f, 32.0f, 32.0f };
    Surface.FillRectangle(HeadIconBox, Theme.WellSurface, 8.0f);
    Surface.StrokeRectangle(HeadIconBox, Theme.BorderSubtle, 8.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::LayersSlabs, VectorIconPlacement{ HeadIconBox.X + 8.0f, HeadIconBox.Y + 8.0f, 16.0f, Theme.TextPrimary, 1.7f });

    const auto& ActiveStudy = Session->QueryActiveStudyDescriptor();
    std::string UpperStudy  = std::string("TEXTURE PAINT · ") + ActiveStudy.Label;
    for (char& Ch : UpperStudy)
    {
        Ch = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
    }
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { HeadIconBox.Right() + 10.0f, CurY }, 13.5f, Theme.TextPrimary, "Layer Inspector");
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { HeadIconBox.Right() + 10.0f, CurY + 18.0f }, InnerW - 186.0f, 8.0f, Theme.TextFaint, UpperStudy);

    float HBtnX = Bounds.Right() - PadX - 26.0f;
    const float HBtnY = CurY + 4.0f;
    if (ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 26.0f, 26.0f }, ControlIcon::RefreshCycle, false, true, Theme, Cursor, Pressed))
    {
        Session->ResetDefaults();
    }
    HBtnX -= 29.0f;
    (void)ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 26.0f, 26.0f }, ControlIcon::DownloadExport, false, true, Theme, Cursor, Pressed);
    HBtnX -= 29.0f;
    (void)ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 26.0f, 26.0f }, ControlIcon::UploadImport, false, true, Theme, Cursor, Pressed);
    HBtnX -= 29.0f;
    if (ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 26.0f, 26.0f }, ControlIcon::RedoArrow, false, Session->QueryRedoAvailable(), Theme, Cursor, Pressed))
    {
        Session->ApplyRedo();
    }
    HBtnX -= 29.0f;
    if (ConstructHeaderIconButton(Surface, { HBtnX, HBtnY, 26.0f, 26.0f }, ControlIcon::UndoArrow, false, Session->QueryUndoAvailable(), Theme, Cursor, Pressed))
    {
        Session->ApplyUndo();
    }
    CurY += 40.0f;
    Surface.StrokeLine({ Bounds.X, CurY }, { Bounds.Right(), CurY }, Theme.BorderSubtle, 1.0f);
    CurY += 10.0f;

    PaintLayerEntry* SelLayer = Session->AccessSelectedLayer();
    if (SelLayer == nullptr)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { Bounds.X + PadX, CurY + 24.0f }, 10.5f, Theme.TextMuted, "No layer selected.");
        Surface.PopClip();
        return;
    }

    //======================================================================================================================
    // 2. IDENTITY BLOCK (Category / Index | Editable Name | Subtitle + Right Active Layer Switcher)
    //======================================================================================================================
    int32_t DisplayRank = 1;
    for (auto It = Session->Layers.rbegin(); It != Session->Layers.rend(); ++It)
    {
        if (It->Id == SelLayer->Id) break;
        ++DisplayRank;
    }
    char CatRankBuf[64];
    std::snprintf(CatRankBuf, sizeof(CatRankBuf), "%s  /  %02d", QueryCategoryUpperTag(SelLayer->Category), DisplayRank);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { Bounds.X + PadX, CurY }, 8.2f, Theme.TextFaint, CatRankBuf);

    const float SwitcherW = 148.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { Bounds.Right() - PadX - SwitcherW, CurY }, 8.0f, Theme.TextFaint, "ACTIVE LAYER");

    const Rectangle2D LayerNameBoxR{ Bounds.X + PadX, CurY + 15.0f, InnerW - SwitcherW - 10.0f, 28.0f };
    if (!LayerNameBuffer.Active && std::string_view(LayerNameBuffer.Text, LayerNameBuffer.Length) != SelLayer->Name)
    {
        LayerNameBuffer.Begin(SelLayer->Name.c_str());
        LayerNameBuffer.Active = false;
    }
    Surface.FillRectangle(LayerNameBoxR, Theme.WellSurface, 8.0f);
    Surface.StrokeRectangle(LayerNameBoxR, LayerNameBuffer.Active ? Theme.BorderStrong : Theme.BorderSubtle, 8.0f, 1.0f);
    const ControlContact NameContact = TextInputField::Construct(
        Surface,
        { LayerNameBoxR.X + 6.0f, LayerNameBoxR.Y + 1.0f, LayerNameBoxR.Width - 12.0f, LayerNameBoxR.Height - 2.0f },
        LayerNameBuffer,
        Input.PrimaryPointer,
        11.0f);
    if ((NameContact.Clicked || LayerNameBuffer.Finalized) && LayerNameBuffer.Length > 0u && !SelLayer->Locked)
    {
        SelLayer->Name.assign(LayerNameBuffer.Text, LayerNameBuffer.Length);
    }

    const Rectangle2D SwitcherBtnR{ Bounds.Right() - PadX - SwitcherW, CurY + 15.0f, SwitcherW, 28.0f };
    if (ConstructHeaderActionCapsule(Surface, SwitcherBtnR, ControlIcon::Count, SelLayer->Name, true, LayerSwitcherOpen, Theme, Cursor, Pressed))
    {
        LayerSwitcherOpen   = !LayerSwitcherOpen;
        BlendDropdownOpen   = false;
        PartDropdownOpen    = false;
        MaskGenDropdownOpen = false;
    }

    std::string UpperSub = SelLayer->FormatSubtitle();
    for (char& Ch : UpperSub)
    {
        Ch = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
    }
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { Bounds.X + PadX, CurY + 47.0f }, InnerW, 8.2f, Theme.TextFaint, UpperSub);
    CurY += 64.0f;

    //======================================================================================================================
    // 3. THREE METRIC TILES (OPACITY 100% | CHANNELS 4 / 22 + Group Meter | MASK None)
    //======================================================================================================================
    const float MetricGap = 8.0f;
    const float MetricW   = (InnerW - MetricGap * 2.0f) / 3.0f;
    const Rectangle2D MetricOpR{ Bounds.X + PadX, CurY, MetricW, 56.0f };
    const Rectangle2D MetricChR{ MetricOpR.Right() + MetricGap, CurY, MetricW, 56.0f };
    const Rectangle2D MetricMkR{ MetricChR.Right() + MetricGap, CurY, MetricW, 56.0f };

    Surface.FillRectangle(MetricOpR, Theme.SectionSurface, 10.0f);
    Surface.FillRectangle(MetricChR, Theme.SectionSurface, 10.0f);
    Surface.FillRectangle(MetricMkR, Theme.SectionSurface, 10.0f);

    char OpValBuf[24];
    std::snprintf(OpValBuf, sizeof(OpValBuf), "%d%%", static_cast<int>(std::lround(SelLayer->Opacity * 100.0f)));
    const auto& BlendNames = TexturePaintSession::QueryBlendMethodLabels();
    const char* BlendLabel = BlendNames[std::min<std::size_t>(static_cast<std::size_t>(SelLayer->BlendMethod), BlendNames.size() - 1u)];
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { MetricOpR.X + 10.0f, MetricOpR.Y + 7.0f }, 7.8f, Theme.TextFaint, "OPACITY");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold,     { MetricOpR.X + 10.0f, MetricOpR.Y + 19.0f }, 13.5f, Theme.TextPrimary, OpValBuf);
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { MetricOpR.X + 10.0f, MetricOpR.Y + 38.0f }, MetricW - 16.0f, 8.2f, Theme.TextMuted, BlendLabel);

    char ChMetricBuf[32];
    std::snprintf(ChMetricBuf, sizeof(ChMetricBuf), "%zu / %zu", SelLayer->CountActiveChannels(), TexturePaintSession::ChannelCount);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { MetricChR.X + 10.0f, MetricChR.Y + 7.0f }, 7.8f, Theme.TextFaint, "CHANNELS");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold,     { MetricChR.X + 10.0f, MetricChR.Y + 19.0f }, 13.5f, Theme.TextPrimary, ChMetricBuf);

    constexpr std::array<ColorRgba, 5u> GroupSegmentTints {{
        { 232u, 194u, 122u, 255u },
        { 143u, 163u, 191u, 255u },
        { 207u, 227u, 255u, 255u },
        { 158u, 230u, 216u, 255u },
        { 255u, 122u,  59u, 255u },
    }};
    const float SegTotalW = MetricW - 20.0f;
    const float SegW      = (SegTotalW - 8.0f) / 5.0f;
    const auto& ChannelReg = TexturePaintSession::QueryChannelRegistry();
    for (std::size_t G = 0u; G < 5u; ++G)
    {
        bool AnyInGroup = false;
        for (std::size_t C = 0u; C < TexturePaintSession::ChannelCount; ++C)
        {
            if (static_cast<std::size_t>(ChannelReg[C].Group) == G && SelLayer->ActiveChannels[C])
            {
                AnyInGroup = true;
                break;
            }
        }
        const Rectangle2D SegR{ MetricChR.X + 10.0f + static_cast<float>(G) * (SegW + 2.0f), MetricChR.Y + 42.0f, SegW, 4.5f };
        Surface.FillRectangle(SegR, AnyInGroup ? GroupSegmentTints[G] : Theme.WellSurface, 2.0f);
    }

    const char* MaskStatusStr = SelLayer->Mask.Attached ? (SelLayer->Mask.Enabled ? "Active" : "Muted") : "None";
    const char* MaskSubStr    = SelLayer->Mask.Attached ? "Manual paint" : "No mask";
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::SemiBold, { MetricMkR.X + 10.0f, MetricMkR.Y + 7.0f }, 7.8f, Theme.TextFaint, "MASK");
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Bold,     { MetricMkR.X + 10.0f, MetricMkR.Y + 19.0f }, 13.5f, Theme.TextPrimary, MaskStatusStr);
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { MetricMkR.X + 10.0f, MetricMkR.Y + 38.0f }, MetricW - 16.0f, 8.2f, Theme.TextMuted, MaskSubStr);

    CurY += 64.0f;
    Surface.StrokeLine({ Bounds.X, CurY }, { Bounds.Right(), CurY }, Theme.BorderSubtle, 1.0f);
    CurY += 4.0f;

    //======================================================================================================================
    // 4. SCROLLABLE INSPECTOR BODY (reserving 28px for footer)
    //======================================================================================================================
    constexpr float FooterH = 28.0f;
    const Rectangle2D BodyViewportR{ Bounds.X, CurY, Bounds.Width, std::max(48.0f, Bounds.Bottom() - FooterH - CurY) };
    const bool PopupOpen = LayerSwitcherOpen || BlendDropdownOpen || PartDropdownOpen || MaskGenDropdownOpen;

    if (!PopupOpen && BodyViewportR.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY = ControlAppearance::AdvanceScrollOffset(
            ScrollOffsetY,
            Input.WheelDeltaY * 34.0f,
            BodyViewportR.Height + MaxScrollY,
            BodyViewportR.Height);
    }
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, MaxScrollY);

    Surface.PushClip(BodyViewportR);
    float Sy = BodyViewportR.Y + 8.0f - ScrollOffsetY;
    const bool AllowBodyClicks = !PopupOpen && BodyViewportR.Contains(Cursor);
    PaneInputReading BodyInput = Input;
    if (!AllowBodyClicks)
    {
        BodyInput.PrimaryPointer.Pressed  = false;
        BodyInput.PrimaryPointer.Released = false;
        BodyInput.PrimaryPointer.Down     = false;
    }

    //----------------------------------------------------------------------------------------------------------------------
    // 4A. LAYER SECTION (BLEND + PART side-by-side dropdowns, Opacity slider, Visible & Lock switches)
    //----------------------------------------------------------------------------------------------------------------------
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { Bounds.X + PadX, Sy }, 9.2f, Theme.TextFaint, "LAYER");
    Sy += 18.0f;

    const float HalfColW = (InnerW - 8.0f) * 0.5f;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { Bounds.X + PadX, Sy }, 7.8f, Theme.TextFaint, "BLEND");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { Bounds.X + PadX + HalfColW + 8.0f, Sy }, 7.8f, Theme.TextFaint, "PART");
    Sy += 14.0f;

    const Rectangle2D BlendBtnR{ Bounds.X + PadX, Sy, HalfColW, 28.0f };
    const Rectangle2D PartBtnR { Bounds.X + PadX + HalfColW + 8.0f, Sy, HalfColW, 28.0f };

    if (ConstructHeaderActionCapsule(Surface, BlendBtnR, ControlIcon::Count, BlendLabel, true, BlendDropdownOpen, Theme, Cursor, AllowBodyClicks && Pressed))
    {
        BlendDropdownOpen = !BlendDropdownOpen;
        PartDropdownOpen  = false;
    }

    const auto& PartLabels = TexturePaintSession::QueryTeapotPartLabels();
    const char* PartLabel  = (SelLayer->PartFilter >= 0 && SelLayer->PartFilter < 4)
        ? PartLabels[static_cast<std::size_t>(SelLayer->PartFilter)]
        : "All parts";
    if (ConstructHeaderActionCapsule(Surface, PartBtnR, ControlIcon::Count, PartLabel, true, PartDropdownOpen, Theme, Cursor, AllowBodyClicks && Pressed))
    {
        PartDropdownOpen  = !PartDropdownOpen;
        BlendDropdownOpen = false;
    }
    Sy += 34.0f;

    if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Opacity", SelLayer->Opacity, 0.0f, 1.0f, "%.0f %%", 100.0f, BodyInput, Theme))
    {
        Session->MarkContentModified();
    }
    Sy += 48.0f;

    if (ConstructSwitchControlRow(Surface, { Bounds.X + PadX, Sy, InnerW, 30.0f }, "Visible", SelLayer->Visible, Theme, Cursor, AllowBodyClicks && Pressed))
    {
        Session->MarkContentModified();
    }
    Sy += 32.0f;
    (void)ConstructSwitchControlRow(Surface, { Bounds.X + PadX, Sy, InnerW, 30.0f }, "Lock edits", SelLayer->Locked, Theme, Cursor, AllowBodyClicks && Pressed);
    Sy += 38.0f;

    Surface.StrokeLine({ Bounds.X + PadX, Sy - 4.0f }, { Bounds.Right() - PadX, Sy - 4.0f }, Theme.BorderSubtle, 1.0f);

    //----------------------------------------------------------------------------------------------------------------------
    // 4B. KIND-SPECIFIC SECTION (Paint layer / Fill & pattern / Material / Generator / Gradient points / Text & stamp)
    //----------------------------------------------------------------------------------------------------------------------
    if (SelLayer->Category == PaintLayerCategory::Gradient)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Bold, { Bounds.X + PadX, Sy + 4.0f }, 9.2f, Theme.TextFaint, "GRADIENT POINTS");

        const Rectangle2D RemPtBtnR{ Bounds.Right() - PadX - 62.0f, Sy, 62.0f, 24.0f };
        const Rectangle2D AddPtBtnR{ RemPtBtnR.X - 6.0f - 54.0f, Sy, 54.0f, 24.0f };
        if (ConstructHeaderActionCapsule(Surface, AddPtBtnR, ControlIcon::PlusAdd, "Add", false, false, Theme, Cursor, AllowBodyClicks && Pressed))
        {
            Session->AddGradientPointToSelectedLayer();
        }
        if (ConstructHeaderActionCapsule(Surface, RemPtBtnR, ControlIcon::Count, "Remove", false, false, Theme, Cursor, AllowBodyClicks && Pressed))
        {
            Session->RemoveGradientPointFromSelectedLayer();
        }
        Sy += 28.0f;

        for (std::size_t PtIdx = 0u; PtIdx < SelLayer->GradientPoints.size(); ++PtIdx)
        {
            const auto& Pt = SelLayer->GradientPoints[PtIdx];
            const Rectangle2D PtRowR{ Bounds.X + PadX, Sy, InnerW, 32.0f };
            const bool PtSelected = (SelLayer->SelectedGradientPointIndex == static_cast<int32_t>(PtIdx));
            const bool PtHover    = PtRowR.Contains(Cursor);

            Surface.FillRectangle(PtRowR, PtSelected ? StudioRowSelected : (PtHover ? StudioRowHover : Theme.SectionSurface), 7.0f);
            if (PtSelected)
            {
                Surface.FillRectangle({ PtRowR.X, PtRowR.Y + 5.0f, 3.0f, PtRowR.Height - 10.0f }, ColorRgba{ 255u, 255u, 255u, 255u }, 1.5f);
            }
            Surface.FillCircle({ PtRowR.X + 15.0f, PtRowR.Y + 16.0f }, 6.0f, Pt.TintColor);
            Surface.StrokeCircle({ PtRowR.X + 15.0f, PtRowR.Y + 16.0f }, 6.0f, Theme.BorderStrong, 1.0f);

            WorkspaceAppearance::ConstructWeightedText(
                Surface, PtSelected ? FontWeightClass::SemiBold : FontWeightClass::Medium, { PtRowR.X + 28.0f, PtRowR.Y + 8.0f }, 9.8f, Theme.TextPrimary, Pt.Label);

            char PtMeta[64];
            std::snprintf(
                PtMeta,
                sizeof(PtMeta),
                "rough %d · metal %d · height %d",
                static_cast<int>(std::lround(Pt.Roughness * 100.0f)),
                static_cast<int>(std::lround(Pt.Metalness * 100.0f)),
                static_cast<int>(std::lround(Pt.Height * 100.0f)));
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { PtRowR.Right() - 142.0f, PtRowR.Y + 9.0f }, 8.2f, Theme.TextFaint, PtMeta);

            if (AllowBodyClicks && Pressed && PtHover)
            {
                SelLayer->SelectedGradientPointIndex = static_cast<int32_t>(PtIdx);
            }
            Sy += 34.0f;
        }

        const Rectangle2D BgRowR{ Bounds.X + PadX, Sy, InnerW, 32.0f };
        const bool BgSelected = (SelLayer->SelectedGradientPointIndex == -1);
        const bool BgHover    = BgRowR.Contains(Cursor);
        Surface.FillRectangle(BgRowR, BgSelected ? StudioRowSelected : (BgHover ? StudioRowHover : Theme.SectionSurface), 7.0f);
        if (BgSelected)
        {
            Surface.FillRectangle({ BgRowR.X, BgRowR.Y + 5.0f, 3.0f, BgRowR.Height - 10.0f }, ColorRgba{ 255u, 255u, 255u, 255u }, 1.5f);
        }
        Surface.FillCircle({ BgRowR.X + 15.0f, BgRowR.Y + 16.0f }, 6.0f, SelLayer->GradientBackgroundTint);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, BgSelected ? FontWeightClass::SemiBold : FontWeightClass::Medium, { BgRowR.X + 28.0f, BgRowR.Y + 8.0f }, 9.8f, Theme.TextPrimary, "Background");
        if (AllowBodyClicks && Pressed && BgHover)
        {
            SelLayer->SelectedGradientPointIndex = -1;
        }
        Sy += 38.0f;

        if (SelLayer->SelectedGradientPointIndex >= 0 &&
            static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex) < SelLayer->GradientPoints.size())
        {
            auto& ActivePt = SelLayer->GradientPoints[static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex)];
            if (ConstructColourSwatchRow(
                    Surface,
                    { Bounds.X + PadX, Sy, InnerW, 32.0f },
                    "Colour",
                    ActivePt.TintColor,
                    TexturePaintSession::QueryBrushQuickSwatches(),
                    Theme,
                    Cursor,
                    AllowBodyClicks && Pressed))
            {
                Session->MarkContentModified();
            }
            Sy += 36.0f;
            if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Radius", ActivePt.Radius, 0.15f, 2.50f, "%.0f %%", 100.0f, BodyInput, Theme))
            {
                Session->MarkContentModified();
            }
            Sy += 48.0f;
            if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Strength", ActivePt.Strength, 0.0f, 2.00f, "%.0f %%", 100.0f, BodyInput, Theme))
            {
                Session->MarkContentModified();
            }
            Sy += 48.0f;
        }
        if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Falloff", SelLayer->GradientFalloff, 0.25f, 3.00f, "%.0f %%", 100.0f, BodyInput, Theme))
        {
            Session->MarkContentModified();
        }
        Sy += 48.0f;
    }
    else
    {
        const char* KindTitle = "Paint layer";
        const char* KindSub   = "Hand-painted texels · uses the active Brush settings";
        if (SelLayer->Category == PaintLayerCategory::Fill)
        {
            KindTitle = "Fill & pattern";
            KindSub   = "Flat colour & repeating pattern";
        }
        else if (SelLayer->Category == PaintLayerCategory::Material)
        {
            KindTitle = "Material preset";
            KindSub   = "Frontier PBR material preset & grain";
        }
        else if (SelLayer->Category == PaintLayerCategory::WearGen)
        {
            KindTitle = "Procedural generator";
            KindSub   = "Curvature, cavity & directional mask";
        }
        else if (SelLayer->Category == PaintLayerCategory::Stamp || SelLayer->Category == PaintLayerCategory::Text)
        {
            KindTitle = "Text & stamp";
            KindSub   = "Decal placement & lettering";
        }

        const Rectangle2D KindFoldR{ Bounds.X + PadX, Sy + 2.0f, InnerW, 38.0f };
        const bool KindHover = KindFoldR.Contains(Cursor);
        Surface.FillRectangle(KindFoldR, KindHover ? StudioRowHover : Theme.SectionSurface, 8.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { KindFoldR.X + 10.0f, KindFoldR.Y + 5.0f }, 10.0f, Theme.TextPrimary, KindTitle);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { KindFoldR.X + 10.0f, KindFoldR.Y + 20.0f }, KindFoldR.Width - 34.0f, 8.2f, Theme.TextFaint, KindSub);
        VectorIconRepository::ConstructControl(
            Surface,
            KindSectionOpen ? ControlIcon::ChevronDown : ControlIcon::ChevronForward,
            VectorIconPlacement{ KindFoldR.Right() - 20.0f, KindFoldR.Y + 13.0f, 11.0f, Theme.TextMuted, 1.7f });

        if (AllowBodyClicks && Pressed && KindFoldR.Contains(Cursor))
        {
            KindSectionOpen = !KindSectionOpen;
        }
        Sy += 46.0f;

        if (KindSectionOpen)
        {
            if (SelLayer->Category == PaintLayerCategory::Paint)
            {
                const Rectangle2D OpenBrushBtnR{ Bounds.X + PadX, Sy, InnerW, 28.0f };
                if (ConstructHeaderActionCapsule(Surface, OpenBrushBtnR, ControlIcon::BrushTool, "Open the Brush Studio", false, false, Theme, Cursor, AllowBodyClicks && Pressed))
                {
                    if (OpenBrushStudioCallback)
                    {
                        OpenBrushStudioCallback();
                    }
                }
                Sy += 34.0f;
            }
            else if (SelLayer->Category == PaintLayerCategory::Fill)
            {
                if (ConstructColourSwatchRow(
                        Surface,
                        { Bounds.X + PadX, Sy, InnerW, 32.0f },
                        "Base colour",
                        SelLayer->PrimerTint,
                        TexturePaintSession::QueryBrushQuickSwatches(),
                        Theme,
                        Cursor,
                        AllowBodyClicks && Pressed))
                {
                    SelLayer->ChannelTints[static_cast<std::size_t>(PaintSurfaceChannel::Albedo)] = SelLayer->PrimerTint;
                    Session->MarkContentModified();
                }
                Sy += 36.0f;
                float RepVal = static_cast<float>(SelLayer->PatternRepeats);
                if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Pattern repeats", RepVal, 1.0f, 16.0f, "%.0fx", 1.0f, BodyInput, Theme))
                {
                    SelLayer->PatternRepeats = static_cast<int32_t>(std::lround(RepVal));
                    Session->MarkContentModified();
                }
                Sy += 48.0f;
                if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Height gain", SelLayer->PatternHeightGain, 0.0f, 1.0f, "%.0f %%", 100.0f, BodyInput, Theme))
                {
                    Session->MarkContentModified();
                }
                Sy += 48.0f;
            }
            else if (SelLayer->Category == PaintLayerCategory::Material)
            {
                if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Grain scale", SelLayer->GrainScale, 1.0f, 32.0f, "%.0fx", 1.0f, BodyInput, Theme))
                {
                    Session->MarkContentModified();
                }
                Sy += 48.0f;
                if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Grain contrast", SelLayer->GrainContrast, 0.0f, 1.0f, "%.0f %%", 100.0f, BodyInput, Theme))
                {
                    Session->MarkContentModified();
                }
                Sy += 48.0f;
            }
            else if (SelLayer->Category == PaintLayerCategory::WearGen)
            {
                if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Threshold", SelLayer->GeneratorThreshold, 0.05f, 0.95f, "%.0f %%", 100.0f, BodyInput, Theme))
                {
                    Session->MarkContentModified();
                }
                Sy += 48.0f;
                if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Softness", SelLayer->GeneratorSoftness, 0.05f, 0.95f, "%.0f %%", 100.0f, BodyInput, Theme))
                {
                    Session->MarkContentModified();
                }
                Sy += 48.0f;
                if (ConstructSwitchControlRow(Surface, { Bounds.X + PadX, Sy, InnerW, 30.0f }, "Invert wear", SelLayer->GeneratorInvert, Theme, Cursor, AllowBodyClicks && Pressed))
                {
                    Session->MarkContentModified();
                }
                Sy += 34.0f;
            }
        }
    }

    Surface.StrokeLine({ Bounds.X + PadX, Sy }, { Bounds.Right() - PadX, Sy }, Theme.BorderSubtle, 1.0f);
    Sy += 8.0f;

    //----------------------------------------------------------------------------------------------------------------------
    // 4C. CHANNELS STACK (5 Collapsible Groups: Core, Surface, Coat, Optics, Emission + Bulk Pills All / Core / One)
    //----------------------------------------------------------------------------------------------------------------------
    std::string ChannelsHeaderTitle = "CHANNELS";
    if (SelLayer->Category == PaintLayerCategory::Gradient)
    {
        if (SelLayer->SelectedGradientPointIndex >= 0 &&
            static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex) < SelLayer->GradientPoints.size())
        {
            std::string UpperPt = SelLayer->GradientPoints[static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex)].Label;
            for (char& Ch : UpperPt) Ch = static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
            ChannelsHeaderTitle = "CHANNELS · " + UpperPt;
        }
        else
        {
            ChannelsHeaderTitle = "CHANNELS · BACKGROUND";
        }
    }
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { Bounds.X + PadX, Sy + 4.0f }, 9.2f, Theme.TextFaint, ChannelsHeaderTitle);

    constexpr std::array<const char*, 3u> BulkLabels {{ "All", "Core", "One" }};
    float BulkX = Bounds.Right() - PadX - 118.0f;
    for (uint32_t B = 0u; B < 3u; ++B)
    {
        const Rectangle2D PillR{ BulkX, Sy, 36.0f, 22.0f };
        const bool Hover = PillR.Contains(Cursor);
        Surface.FillRectangle(PillR, Hover ? Theme.HoverSurface : Theme.WellSurface, 6.0f);
        Surface.StrokeRectangle(PillR, Theme.BorderSubtle, 6.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { PillR.X + 8.0f, PillR.Y + 3.5f }, 8.6f, Theme.TextPrimary, BulkLabels[B]);
        if (AllowBodyClicks && Pressed && Hover)
        {
            Session->ApplyChannelBulkPreset(B);
        }
        BulkX += 40.0f;
    }
    Sy += 28.0f;

    const Rectangle2D ChSearchR{ Bounds.X + PadX, Sy, InnerW, 28.0f };
    Surface.FillRectangle(ChSearchR, Theme.WellSurface, 8.0f);
    Surface.StrokeRectangle(ChSearchR, ChannelSearchBuffer.Active ? Theme.BorderStrong : Theme.BorderSubtle, 8.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::SearchGlass, VectorIconPlacement{ ChSearchR.X + 8.0f, ChSearchR.Y + 7.5f, 13.0f, Theme.TextFaint, 1.65f });
    if (ChannelSearchBuffer.Length == 0u && !ChannelSearchBuffer.Active)
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { ChSearchR.X + 26.0f, ChSearchR.Y + 6.5f }, ChSearchR.Width - 32.0f, 9.0f, Theme.TextFaint, "Filter 22 channels (albedo, clearcoat, sheen...)");
    }
    const ControlContact ChSearchContact = TextInputField::Construct(
        Surface,
        { ChSearchR.X + 22.0f, ChSearchR.Y + 1.0f, ChSearchR.Width - 26.0f, ChSearchR.Height - 2.0f },
        ChannelSearchBuffer,
        BodyInput.PrimaryPointer,
        9.2f);
    if (ChSearchContact.Clicked || ChannelSearchBuffer.Active || ChannelSearchBuffer.Finalized)
    {
        Session->ChannelQuery.assign(ChannelSearchBuffer.Text, ChannelSearchBuffer.Length);
    }
    Sy += 34.0f;

    const auto& GroupNames = TexturePaintSession::QueryChannelGroupLabels();
    for (std::size_t G = 0u; G < 5u; ++G)
    {
        const PaintChannelGroup GroupEnum = static_cast<PaintChannelGroup>(G);
        std::size_t GroupTotal    = 0u;
        std::size_t GroupActive   = 0u;
        std::size_t GroupMatching = 0u;

        for (std::size_t C = 0u; C < TexturePaintSession::ChannelCount; ++C)
        {
            if (ChannelReg[C].Group == GroupEnum)
            {
                ++GroupTotal;
                if (SelLayer->ActiveChannels[C]) ++GroupActive;
                if (ChannelMatchesQuery(ChannelReg[C], Session->ChannelQuery)) ++GroupMatching;
            }
        }
        if (GroupMatching == 0u)
        {
            continue;
        }

        const Rectangle2D GroupHeadR{ Bounds.X + PadX, Sy, InnerW, 30.0f };
        const bool GroupHover = GroupHeadR.Contains(Cursor);
        const bool GroupOpen  = Session->ChannelGroupOpen[G] || !Session->ChannelQuery.empty();
        Surface.FillRectangle(GroupHeadR, GroupHover ? StudioRowHover : Theme.SectionSurface, 7.0f);

        VectorIconRepository::ConstructControl(
            Surface,
            GroupOpen ? ControlIcon::ChevronDown : ControlIcon::ChevronForward,
            VectorIconPlacement{ GroupHeadR.X + 8.0f, GroupHeadR.Y + 9.5f, 11.0f, Theme.TextMuted, 1.7f });
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { GroupHeadR.X + 24.0f, GroupHeadR.Y + 7.0f }, 9.8f, Theme.TextPrimary, GroupNames[G]);

        const bool AllGroupOn = (GroupActive == GroupTotal && GroupTotal > 0u);
        const Rectangle2D GSwitchR{ GroupHeadR.Right() - 40.0f, GroupHeadR.Y + 6.0f, 32.0f, 18.0f };
        const ColorRgba GTrackFill = AllGroupOn ? ColorRgba{ 238u, 238u, 236u, 255u } : Theme.WellSurface;
        const ColorRgba GKnobFill  = AllGroupOn ? ColorRgba{ 20u, 21u, 24u, 255u } : Theme.TextMuted;
        Surface.FillRectangle(GSwitchR, GTrackFill, 9.0f);
        Surface.StrokeRectangle(GSwitchR, AllGroupOn ? GTrackFill : Theme.BorderStrong, 9.0f, 1.0f);
        Surface.FillCircle({ AllGroupOn ? (GSwitchR.Right() - 9.0f) : (GSwitchR.X + 9.0f), GSwitchR.Y + 9.0f }, 5.8f, GKnobFill);

        char GroupCountBuf[24];
        std::snprintf(GroupCountBuf, sizeof(GroupCountBuf), "%zu/%zu on", GroupActive, GroupTotal);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { GSwitchR.X - 44.0f, GroupHeadR.Y + 8.0f }, 8.4f, Theme.TextFaint, GroupCountBuf);

        if (AllowBodyClicks && Pressed && GroupHover)
        {
            if (GSwitchR.Contains(Cursor))
            {
                Session->ToggleChannelGroupOnSelectedLayer(GroupEnum);
            }
            else
            {
                Session->ChannelGroupOpen[G] = !Session->ChannelGroupOpen[G];
            }
        }
        Sy += 33.0f;

        if (!GroupOpen)
        {
            continue;
        }

        for (std::size_t C = 0u; C < TexturePaintSession::ChannelCount; ++C)
        {
            const auto& Desc = ChannelReg[C];
            if (Desc.Group != GroupEnum || !ChannelMatchesQuery(Desc, Session->ChannelQuery))
            {
                continue;
            }
            const bool ChOn       = SelLayer->ActiveChannels[C];
            const bool ChExpanded = SelLayer->ExpandedChannels[C] && ChOn;

            ColorRgba SwatchCol = Desc.ColorChannel
                ? SelLayer->ChannelTints[C]
                : Desc.SwatchTint;
            float ScalarNorm = (SelLayer->ChannelScalars[C] - Desc.DefaultMin) / std::max(0.01f, Desc.DefaultMax - Desc.DefaultMin);

            if (SelLayer->Category == PaintLayerCategory::Gradient &&
                SelLayer->SelectedGradientPointIndex >= 0 &&
                static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex) < SelLayer->GradientPoints.size())
            {
                const auto& Pt = SelLayer->GradientPoints[static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex)];
                if (Desc.Key == PaintSurfaceChannel::Albedo)         SwatchCol  = Pt.TintColor;
                else if (Desc.Key == PaintSurfaceChannel::Roughness) ScalarNorm = Pt.Roughness;
                else if (Desc.Key == PaintSurfaceChannel::Metalness) ScalarNorm = Pt.Metalness;
                else if (Desc.Key == PaintSurfaceChannel::Height)    ScalarNorm = (Pt.Height + 1.0f) * 0.5f;
            }

            char ReadoutBuf[24];
            if (!ChOn)
            {
                std::snprintf(ReadoutBuf, sizeof(ReadoutBuf), "Off");
            }
            else if (SelLayer->Category == PaintLayerCategory::Paint)
            {
                std::snprintf(ReadoutBuf, sizeof(ReadoutBuf), "Brush");
            }
            else if (Desc.ColorChannel)
            {
                std::snprintf(ReadoutBuf, sizeof(ReadoutBuf), "#%02X%02X%02X", SwatchCol.Red, SwatchCol.Green, SwatchCol.Blue);
            }
            else
            {
                std::snprintf(ReadoutBuf, sizeof(ReadoutBuf), "%d %%", static_cast<int>(std::lround(ScalarNorm * 100.0f)));
            }

            const ChannelEntryRowOutcome ChAct = ConstructChannelEntryRow(
                Surface,
                { Bounds.X + PadX, Sy, InnerW, 36.0f },
                SwatchCol,
                Desc.ColorChannel,
                ScalarNorm,
                Desc.Label,
                Desc.SubHint,
                ReadoutBuf,
                ChOn,
                ChExpanded,
                Theme,
                Cursor,
                AllowBodyClicks && Pressed);

            if (ChAct.SwitchToggled)
            {
                Session->ToggleChannelOnSelectedLayer(Desc.Key);
            }
            else if (ChAct.RowClicked)
            {
                if (!ChOn)
                {
                    Session->AddChannelToSelectedLayer(Desc.Key);
                }
                else
                {
                    SelLayer->ExpandedChannels[C] = !SelLayer->ExpandedChannels[C];
                    SelLayer->InspectedChannel    = Desc.Key;
                }
            }
            Sy += 38.0f;

            if (SelLayer->ExpandedChannels[C] && SelLayer->ActiveChannels[C])
            {
                const float BoxH = (SelLayer->Category == PaintLayerCategory::Paint) ? 58.0f : 94.0f;
                const Rectangle2D EditorBoxR{ Bounds.X + PadX + 6.0f, Sy, InnerW - 6.0f, BoxH };
                Surface.FillRectangle(EditorBoxR, Theme.WellSurface, 8.0f);
                Surface.StrokeRectangle(EditorBoxR, Theme.BorderSubtle, 8.0f, 1.0f);

                float Ey = EditorBoxR.Y + 6.0f;
                const float Ex = EditorBoxR.X + 8.0f;
                const float Ew = EditorBoxR.Width - 16.0f;

                if (SelLayer->Category != PaintLayerCategory::Paint)
                {
                    if (Desc.ColorChannel)
                    {
                        ColorRgba* TargetCol = &SelLayer->ChannelTints[C];
                        if (SelLayer->Category == PaintLayerCategory::Gradient &&
                            Desc.Key == PaintSurfaceChannel::Albedo &&
                            SelLayer->SelectedGradientPointIndex >= 0 &&
                            static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex) < SelLayer->GradientPoints.size())
                        {
                            TargetCol = &SelLayer->GradientPoints[static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex)].TintColor;
                        }
                        if (ConstructColourSwatchRow(
                                Surface,
                                { Ex, Ey, Ew, 30.0f },
                                "Tint",
                                *TargetCol,
                                TexturePaintSession::QueryBrushQuickSwatches(),
                                Theme,
                                Cursor,
                                AllowBodyClicks && Pressed))
                        {
                            if (Desc.Key == PaintSurfaceChannel::Albedo)
                            {
                                SelLayer->PrimerTint = *TargetCol;
                            }
                            Session->MarkContentModified();
                        }
                        Ey += 34.0f;
                    }
                    else
                    {
                        float* TargetScalar = &SelLayer->ChannelScalars[C];
                        if (SelLayer->Category == PaintLayerCategory::Gradient &&
                            SelLayer->SelectedGradientPointIndex >= 0 &&
                            static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex) < SelLayer->GradientPoints.size())
                        {
                            auto& Pt = SelLayer->GradientPoints[static_cast<std::size_t>(SelLayer->SelectedGradientPointIndex)];
                            if (Desc.Key == PaintSurfaceChannel::Roughness)      TargetScalar = &Pt.Roughness;
                            else if (Desc.Key == PaintSurfaceChannel::Metalness) TargetScalar = &Pt.Metalness;
                            else if (Desc.Key == PaintSurfaceChannel::Height)    TargetScalar = &Pt.Height;
                        }
                        if (ConstructSliderReadoutRow(
                                Surface,
                                { Ex, Ey, Ew, 44.0f },
                                "Value",
                                *TargetScalar,
                                Desc.DefaultMin,
                                Desc.DefaultMax,
                                "%.0f %%",
                                100.0f,
                                BodyInput,
                                Theme))
                        {
                            Session->MarkContentModified();
                        }
                        Ey += 46.0f;
                    }
                }

                if (ConstructSliderReadoutRow(
                        Surface,
                        { Ex, Ey, Ew, 44.0f },
                        "Channel opacity",
                        SelLayer->ChannelOpacities[C],
                        0.0f,
                        1.0f,
                        "%.0f %%",
                        100.0f,
                        BodyInput,
                        Theme))
                {
                    Session->MarkContentModified();
                }
                Sy += BoxH + 4.0f;
            }
        }
        Sy += 4.0f;
    }

    //----------------------------------------------------------------------------------------------------------------------
    // 4D. MASK SECTION ("MASK", "+ Attach mask" / "Remove", Enabled, Invert, Opacity, Generator)
    //----------------------------------------------------------------------------------------------------------------------
    Sy += 4.0f;
    Surface.StrokeLine({ Bounds.X + PadX, Sy }, { Bounds.Right() - PadX, Sy }, Theme.BorderSubtle, 1.0f);
    Sy += 8.0f;

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { Bounds.X + PadX, Sy + 4.0f }, 9.2f, Theme.TextFaint, "MASK");

    const float MaskBtnW = SelLayer->Mask.Attached ? 72.0f : 102.0f;
    const Rectangle2D MaskAttachBtnR{ Bounds.Right() - PadX - MaskBtnW, Sy, MaskBtnW, 24.0f };
    if (ConstructHeaderActionCapsule(
            Surface,
            MaskAttachBtnR,
            SelLayer->Mask.Attached ? ControlIcon::Count : ControlIcon::PlusAdd,
            SelLayer->Mask.Attached ? "Remove" : "Attach mask",
            false,
            false,
            Theme,
            Cursor,
            AllowBodyClicks && Pressed))
    {
        Session->ToggleOrCreateMaskOnSelectedLayer();
    }
    Sy += 28.0f;

    if (!SelLayer->Mask.Attached)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { Bounds.X + PadX, Sy }, 9.2f, Theme.TextFaint, "No mask attached to this layer.");
        Sy += 22.0f;
    }
    else
    {
        if (ConstructSwitchControlRow(Surface, { Bounds.X + PadX, Sy, InnerW, 30.0f }, "Mask enabled", SelLayer->Mask.Enabled, Theme, Cursor, AllowBodyClicks && Pressed))
        {
            Session->MarkContentModified();
        }
        Sy += 34.0f;
        if (ConstructSwitchControlRow(Surface, { Bounds.X + PadX, Sy, InnerW, 30.0f }, "Invert mask", SelLayer->Mask.Invert, Theme, Cursor, AllowBodyClicks && Pressed))
        {
            Session->MarkContentModified();
        }
        Sy += 34.0f;
        if (ConstructSliderReadoutRow(Surface, { Bounds.X + PadX, Sy, InnerW, 44.0f }, "Mask opacity", SelLayer->Mask.Opacity, 0.0f, 1.0f, "%.0f %%", 100.0f, BodyInput, Theme))
        {
            Session->MarkContentModified();
        }
        Sy += 48.0f;
    }

    const float ContentH = (Sy + ScrollOffsetY) - BodyViewportR.Y + 12.0f;
    MaxScrollY = std::max(0.0f, ContentH - BodyViewportR.Height);
    Surface.PopClip();

    //======================================================================================================================
    // 5. FOOTER ("Saved locally" + "frontier.texture-paint.v2")
    //======================================================================================================================
    const Rectangle2D FooterR{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(FooterR, Theme.LightSurface ? Theme.NestedSurface : WorkspaceSurfaceColors::FooterBackground);
    Surface.StrokeLine({ FooterR.X, FooterR.Y }, { FooterR.Right(), FooterR.Y }, Theme.BorderSubtle, 1.0f);

    Surface.FillCircle({ FooterR.X + PadX + 4.0f, FooterR.Y + 14.0f }, 3.2f, ColorRgba{ 118u, 212u, 134u, 255u });
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { FooterR.X + PadX + 12.0f, FooterR.Y + 8.0f }, InnerW * 0.5f, 8.8f, Theme.TextMuted, Session->InspectorStatus);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { FooterR.Right() - PadX - 118.0f, FooterR.Y + 8.5f }, 8.2f, Theme.TextFaint, "frontier.texture-paint.v2");

    //======================================================================================================================
    // 6. OVERLAY DROPDOWNS (Layer Switcher, Blend Mode, Mesh Part)
    //======================================================================================================================
    if (LayerSwitcherOpen)
    {
        const float SheetH = static_cast<float>(Session->Layers.size()) * 25.0f + 10.0f;
        const Rectangle2D SheetR{ SwitcherBtnR.X, SwitcherBtnR.Bottom() + 4.0f, SwitcherBtnR.Width, SheetH };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);
        std::size_t RowIdx = 0u;
        for (auto It = Session->Layers.rbegin(); It != Session->Layers.rend(); ++It, ++RowIdx)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(RowIdx) * 25.0f, SheetR.Width - 10.0f, 23.0f };
            const bool Active = (It->Id == Session->SelectedLayerId);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 4.0f }, RowR.Width - 16.0f, 9.4f, Theme.TextPrimary, It->Name);
            if (Pressed && Hover)
            {
                Session->SelectedLayerId = It->Id;
                LayerSwitcherOpen        = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !SwitcherBtnR.Contains(Cursor))
        {
            LayerSwitcherOpen = false;
        }
    }

    if (BlendDropdownOpen)
    {
        const Rectangle2D SheetR{ BlendBtnR.X, BlendBtnR.Bottom() + 4.0f, BlendBtnR.Width, static_cast<float>(BlendNames.size()) * 24.0f + 10.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);
        for (std::size_t I = 0u; I < BlendNames.size(); ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(I) * 24.0f, SheetR.Width - 10.0f, 22.0f };
            const bool Active = (static_cast<std::size_t>(SelLayer->BlendMethod) == I);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 3.5f }, RowR.Width - 16.0f, 9.2f, Theme.TextPrimary, BlendNames[I]);
            if (Pressed && Hover)
            {
                SelLayer->BlendMethod = static_cast<PaintBlendMethod>(I);
                Session->MarkContentModified();
                BlendDropdownOpen = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !BlendBtnR.Contains(Cursor))
        {
            BlendDropdownOpen = false;
        }
    }

    if (PartDropdownOpen)
    {
        constexpr std::array<const char*, 5u> PartOptions {{ "All parts", "Body", "Lid", "Handle", "Spout" }};
        const Rectangle2D SheetR{ PartBtnR.X, PartBtnR.Bottom() + 4.0f, PartBtnR.Width, static_cast<float>(PartOptions.size()) * 24.0f + 10.0f };
        Surface.FillRectangle(SheetR, Theme.NestedSurface, 10.0f);
        Surface.StrokeRectangle(SheetR, Theme.BorderStrong, 10.0f, 1.0f);
        for (std::size_t I = 0u; I < PartOptions.size(); ++I)
        {
            const Rectangle2D RowR{ SheetR.X + 5.0f, SheetR.Y + 5.0f + static_cast<float>(I) * 24.0f, SheetR.Width - 10.0f, 22.0f };
            const int32_t OptFilter = static_cast<int32_t>(I) - 1;
            const bool Active = (SelLayer->PartFilter == OptFilter);
            const bool Hover  = RowR.Contains(Cursor);
            if (Active || Hover)
            {
                Surface.FillRectangle(RowR, Active ? Theme.AccentSoft : Theme.HoverSurface, 6.0f);
            }
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Regular, { RowR.X + 8.0f, RowR.Y + 3.5f }, RowR.Width - 16.0f, 9.2f, Theme.TextPrimary, PartOptions[I]);
            if (Pressed && Hover)
            {
                SelLayer->PartFilter = OptFilter;
                Session->MarkContentModified();
                PartDropdownOpen = false;
                break;
            }
        }
        if (Pressed && !SheetR.Contains(Cursor) && !PartBtnR.Contains(Cursor))
        {
            PartDropdownOpen = false;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
