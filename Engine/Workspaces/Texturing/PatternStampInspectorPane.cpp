/*============================================================================================================================================
                                                        PATTERNSTAMPINSPECTORPANE.CPP
============================================================================================================================================*/
// 🧩 Pattern Studio & Stamp Studio right inspector renderer: KPI summary tiles, Layer Properties, 22 PBR Surface Channels, Layer Mask,
//    8 Non-Destructive Modifiers, Document Ground & Tile, fitted dropdown sheets, and 16-swatch colour picker.

#include "Engine/Workspaces/Texturing/PatternStampInspectorPane.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

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

constexpr std::array<ColorRgba, 16u> PatternStampQuickSwatches {{
    { 239u, 244u, 207u, 255u }, // Studio Cream
    { 118u, 178u, 216u, 255u }, // Cerulean Slate
    { 232u, 194u, 122u, 255u }, // Warm Gold Foil
    { 234u, 236u, 239u, 255u }, // Chalk White
    { 23u,  26u,  31u,  255u }, // Charcoal Ground
    { 11u,  11u,  11u,  255u }, // Obsidian Ink
    { 90u,  118u, 146u, 255u }, // Steel Blue
    { 200u, 120u, 73u,  255u }, // Terracotta Ochre
    { 105u, 156u, 140u, 255u }, // Patina Sage
    { 216u, 144u, 168u, 255u }, // Rosewood Pink
    { 143u, 184u, 222u, 255u }, // Sky Ceramic
    { 52u,  95u,  66u,  255u }, // Racing Green
    { 255u, 122u, 59u,  255u }, // Emissive Amber
    { 168u, 140u, 92u,  255u }, // Brushed Brass
    { 138u, 108u, 168u, 255u }, // Amethyst Dye
    { 246u, 247u, 249u, 255u }, // Porcelain Bright
}};

std::string FormatFixedDecimals(float V, int Decimals) noexcept
{
    char Buf[32];
    if (Decimals <= 0) std::snprintf(Buf, sizeof(Buf), "%d", static_cast<int>(std::lround(V)));
    else if (Decimals == 1) std::snprintf(Buf, sizeof(Buf), "%.1f", V);
    else std::snprintf(Buf, sizeof(Buf), "%.2f", V);
    return std::string(Buf);
}

std::string FormatHexColor(ColorRgba Rgba) noexcept
{
    char Buf[16];
    FormatHexLower(Rgba, Buf, sizeof(Buf));
    return std::string(Buf);
}

void AssignEditText(TextEditBuffer& Buf, std::string_view TextContent) noexcept
{
    const std::size_t CopyLen = std::min(TextContent.size(), static_cast<std::size_t>(TextEditBuffer::Capacity - 1u));
    for (std::size_t I = 0u; I < CopyLen; ++I)
    {
        Buf.Text[I] = TextContent[I];
    }
    Buf.Text[CopyLen]   = '\0';
    Buf.Length          = static_cast<uint32_t>(CopyLen);
    Buf.Caret           = Buf.Length;
    Buf.SelectionAnchor = Buf.Length;
}

ControlIcon QueryIconForModifier(PatternStampModifierCategory Category) noexcept
{
    return (Category == PatternStampModifierCategory::NoiseScale || Category == PatternStampModifierCategory::ScatterDropout)
        ? ControlIcon::NoiseWave
        : ControlIcon::ModifierStar;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

PatternStampInspectorPane::~PatternStampInspectorPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

void PatternStampInspectorPane::AttachSession(PatternStampWorkspaceSession* TargetSession) noexcept
{
    Session = TargetSession;
    CloseAllSheets();
    ActiveSliderId    = -1;
    ScrollOffsetY     = 0.0f;
    SyncedNameLayerId = 0u;
    SyncedTextLayerId = 0u;
}

void PatternStampInspectorPane::CloseAllSheets() noexcept
{
    ActiveSheet        = InspectorSheetVariant::None;
    ActiveSwatchTarget = SwatchTargetChannel::None;
    DropdownScrollY    = 0.0f;
}

void PatternStampInspectorPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##PatternStampInspectorCanvas",
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

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANE RENDERING
//------------------------------------------------------------------------------------------------------------------------

void PatternStampInspectorPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    if (Session == nullptr || Bounds.Width < 160.0f || Bounds.Height < 200.0f)
    {
        return;
    }

    const TexturingThemeColors Theme = QueryTexturingThemeColors();

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground, 0.0f);
    Surface.StrokeLine({ Bounds.X, Bounds.Y }, { Bounds.X, Bounds.Bottom() }, Theme.BorderSubtle, 1.0f);

    const float PadX   = 12.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    const float LeftX  = Bounds.X + PadX;
    float       TopY   = Bounds.Y + 12.0f;

    const bool PatternModeActive = (Session->Mode == PatternStampWorkspaceMode::Pattern);

    // 1. Header
    TopY += ConstructPaneHeadingBar(Surface, LeftX, TopY, InnerW, "Layer & Modifiers", PatternModeActive ? "VECTOR + PBR" : "DECAL + PBR", Theme);
    TopY += 6.0f;

    // 2. Document Title + Active Layer Eyebrow
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY }, InnerW, 17.0f, Theme.TextPrimary, Session->Document.DocumentName);
    TopY += 22.0f;

    PatternStampLayerSpec* Sel = Session->InspectSelectedLayer();
    std::string ActiveUpper = "ACTIVE LAYER: ";
    if (Sel != nullptr)
    {
        for (char C : Sel->Name)
        {
            ActiveUpper.push_back((C >= 'a' && C <= 'z') ? static_cast<char>(C - ('a' - 'A')) : C);
        }
    }
    else
    {
        ActiveUpper += "NONE";
    }
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { LeftX, TopY }, InnerW, 8.6f, Theme.TextFaint, ActiveUpper);
    TopY += 16.0f;

    // 3. 3-Tile KPI Trio (Instances | Scale Span | CoatWeight)
    char ScaleBuf[32];
    std::snprintf(ScaleBuf, sizeof(ScaleBuf), "%.2f-%.2f", Session->MinScaleReading, Session->MaxScaleReading);
    const std::string InstStr = std::to_string(Session->InstancesEvaluated);
    const std::string CovStr  = std::to_string(Session->SurfaceFractionPct) + "%";
    const std::string LaySub  = std::to_string(Session->Document.Layers.size()) + " Layers";

    const std::array<MetricTileReading, 3u> Tiles {{
        { "Instances",  InstStr,  "Evaluated", true  },
        { "Scale Span", ScaleBuf, "Noise Mod", false },
        { "Coverage",   CovStr,   LaySub,      false },
    }};
    TopY += ConstructMetricTrioTiles(Surface, LeftX, TopY, InnerW, Tiles, Theme);
    TopY += 10.0f;

    Surface.StrokeLine({ Bounds.X, TopY }, { Bounds.Right(), TopY }, Theme.BorderSubtle, 1.0f);
    TopY += 6.0f;

    // 4. Scroll Region
    const float FooterH = 26.0f;
    const Rectangle2D ScrollRect{ Bounds.X, TopY, Bounds.Width, std::max(40.0f, Bounds.Bottom() - FooterH - TopY) };

    const bool SheetOpen = (ActiveSheet != InspectorSheetVariant::None) || (ActiveSwatchTarget != SwatchTargetChannel::None);
    if (!SheetOpen && ScrollRect.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }) && std::abs(Input.WheelDeltaY) > 0.001f)
    {
        const float MaxScroll = std::max(0.0f, CachedContentHeight - ScrollRect.Height);
        ScrollOffsetY         = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 34.0f, 0.0f, MaxScroll);
    }

    PaneInputReading BodyInput = Input;
    if (SheetOpen || !ScrollRect.Contains({ Input.PrimaryPointer.X, Input.PrimaryPointer.Y }))
    {
        BodyInput.PrimaryPointer.Pressed  = false;
        BodyInput.PrimaryPointer.Down     = false;
        BodyInput.PrimaryPointer.Released = false;
    }
    if (!Input.PrimaryPointer.Down)
    {
        ActiveSliderId = -1;
    }
    const Point2D BodyCursor { BodyInput.PrimaryPointer.X, BodyInput.PrimaryPointer.Y };
    const bool    BodyPressed = BodyInput.PrimaryPointer.Pressed;

    bool SliderScratch = false;
    auto BeginSlider = [this, &SliderScratch](int32_t SliderId) -> bool&
    {
        SliderScratch = (ActiveSliderId == SliderId);
        return SliderScratch;
    };
    int32_t NextSliderId = 200;

    Rectangle2D CapturedMotifBtn {};
    Rectangle2D CapturedBlendBtn {};
    Rectangle2D CapturedFinishBtn {};
    Rectangle2D CapturedAddChBtn {};
    Rectangle2D CapturedFillSwatch {};
    Rectangle2D CapturedStrokeSwatch {};
    Rectangle2D CapturedGroundSwatch {};

    Surface.PushClip(ScrollRect);
    float CursorY          = ScrollRect.Y + 4.0f - ScrollOffsetY;
    const float ScrollTopY = CursorY;

    if (Sel == nullptr)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX + 8.0f, CursorY + 12.0f }, 10.5f, Theme.TextFaint, "Select a layer to inspect its properties.");
        CursorY += 40.0f;
    }
    else
    {
        const float Ix = LeftX + 12.0f;
        const float Iw = InnerW - 24.0f;

        // =========================================================================
        // A. LAYER PROPERTIES SECTION
        // =========================================================================
        const char* CategoryTag = "MOTIF";
        if (Sel->Category == PatternStampLayerCategory::Typographic) CategoryTag = "TEXT";
        else if (Sel->Category == PatternStampLayerCategory::GradientWash) CategoryTag = "GRADIENT";
        else if (Sel->Category == PatternStampLayerCategory::Folder) CategoryTag = "FOLDER";

        float LayerBodyH = 0.0f;
        if (LayerPropsExpanded)
        {
            LayerBodyH = 54.0f;
            if (Sel->Category == PatternStampLayerCategory::Motif) LayerBodyH += 48.0f;
            else if (Sel->Category == PatternStampLayerCategory::Typographic) LayerBodyH += 54.0f + 52.0f;

            if (Sel->Category != PatternStampLayerCategory::Folder)
            {
                if (Sel->Category != PatternStampLayerCategory::GradientWash) LayerBodyH += 34.0f;
                if (Sel->FillEnabled || Sel->Category == PatternStampLayerCategory::GradientWash)
                {
                    LayerBodyH += 50.0f;
                    LayerBodyH += (Sel->FillSource == PatternStampFillSourceMode::Solid) ? 38.0f : 56.0f;
                }
                LayerBodyH += 52.0f; // Stroke Outline
                if (Sel->StrokeWidthPx > 0.01f || !Sel->FillEnabled) LayerBodyH += 38.0f;
                LayerBodyH += 48.0f;        // Blend Mode
                LayerBodyH += 52.0f * 6.0f; // Opacity, X, Y, W, H, Rotation
                LayerBodyH += 8.0f;
            }
            else
            {
                LayerBodyH += 8.0f;
            }
        }

        const Rectangle2D PropSecR{ LeftX, CursorY, InnerW, 38.0f + LayerBodyH };
        BeginSectionContainer(Surface, PropSecR, Theme, 18.0f);
        if (ConstructSectionHeaderRow(
                Surface,
                { LeftX + 12.0f, CursorY + 11.0f, InnerW - 24.0f, 20.0f },
                ControlIcon::LayersSlabs,
                "Layer Properties",
                CategoryTag,
                LayerPropsExpanded,
                Theme,
                BodyCursor,
                BodyPressed))
        {
            LayerPropsExpanded = !LayerPropsExpanded;
        }

        if (LayerPropsExpanded)
        {
            float Py = CursorY + 42.0f;

            if (SyncedNameLayerId != Sel->Identity)
            {
                SyncedNameLayerId = Sel->Identity;
                AssignEditText(LayerNameBuffer, Sel->Name);
                LayerNameBuffer.Active = false;
            }
            else if (!LayerNameBuffer.Active)
            {
                AssignEditText(LayerNameBuffer, Sel->Name);
            }

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { Ix, Py }, 10.0f, Theme.TextMuted, "Layer Name");
            const Rectangle2D NameBoxR{ Ix, Py + 16.0f, Iw, 28.0f };
            Surface.FillRectangle(NameBoxR, LayerNameBuffer.Active ? Theme.HoverSurface : Theme.WellSurface, 12.0f);
            Surface.StrokeRectangle(NameBoxR, LayerNameBuffer.Active ? Theme.HighlightFill : Theme.BorderSubtle, 12.0f, 1.0f);
            if (BodyPressed) LayerNameBuffer.Active = NameBoxR.Contains(BodyCursor);
            if (LayerNameBuffer.Active)
            {
                WorkspaceAppearance::ForwardInputToTextBuffer(LayerNameBuffer, BodyInput, DeltaSeconds);
                if (LayerNameBuffer.Length > 0u) Sel->Name = std::string(LayerNameBuffer.Text);
            }
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { NameBoxR.X + 12.0f, NameBoxR.Y + 7.0f }, NameBoxR.Width - 24.0f, 10.5f, Theme.TextPrimary, LayerNameBuffer.Text);
            Py += 54.0f;

            if (Sel->Category == PatternStampLayerCategory::Motif)
            {
                const auto& ShapeLabels = PatternStampWorkspaceSession::InspectMotifShapeLabels();
                const std::size_t SIdx  = static_cast<std::size_t>(Sel->MotifShape) % ShapeLabels.size();
                if (ConstructDropdownBarRow(
                        Surface, Ix, Py, Iw, "Motif Shape", ShapeLabels[SIdx], ActiveSheet == InspectorSheetVariant::MotifShape, Theme, BodyInput.PrimaryPointer, CapturedMotifBtn))
                {
                    ActiveSheet        = (ActiveSheet == InspectorSheetVariant::MotifShape) ? InspectorSheetVariant::None : InspectorSheetVariant::MotifShape;
                    ActiveSheetAnchor  = CapturedMotifBtn;
                    ActiveSwatchTarget = SwatchTargetChannel::None;
                    DropdownScrollY    = 0.0f;
                }
                Py += 48.0f;
            }
            else if (Sel->Category == PatternStampLayerCategory::Typographic)
            {
                if (SyncedTextLayerId != Sel->Identity)
                {
                    SyncedTextLayerId = Sel->Identity;
                    AssignEditText(TypographicBuffer, Sel->TextContent);
                    TypographicBuffer.Active = false;
                }
                else if (!TypographicBuffer.Active)
                {
                    AssignEditText(TypographicBuffer, Sel->TextContent);
                }
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Regular, { Ix, Py }, 10.0f, Theme.TextMuted, "Text Content");
                const Rectangle2D TextBoxR{ Ix, Py + 16.0f, Iw, 28.0f };
                Surface.FillRectangle(TextBoxR, TypographicBuffer.Active ? Theme.HoverSurface : Theme.WellSurface, 12.0f);
                Surface.StrokeRectangle(TextBoxR, TypographicBuffer.Active ? Theme.HighlightFill : Theme.BorderSubtle, 12.0f, 1.0f);
                if (BodyPressed) TypographicBuffer.Active = TextBoxR.Contains(BodyCursor);
                if (TypographicBuffer.Active)
                {
                    WorkspaceAppearance::ForwardInputToTextBuffer(TypographicBuffer, BodyInput, DeltaSeconds);
                    const std::string UpdatedText(TypographicBuffer.Text);
                    if (UpdatedText != Sel->TextContent)
                    {
                        Sel->TextContent = UpdatedText;
                        Session->BumpRevision();
                    }
                }
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Regular, { TextBoxR.X + 12.0f, TextBoxR.Y + 7.0f }, TextBoxR.Width - 24.0f, 10.5f, Theme.TextPrimary, TypographicBuffer.Text);
                Py += 54.0f;

                const int32_t FsSid = NextSliderId++;
                bool&         DragFs = BeginSlider(FsSid);
                float         FsNorm = std::clamp((Sel->FontSizePx - 14.0f) / 146.0f, 0.0f, 1.0f);
                if (ConstructSliderReadoutRow(
                        Surface, Ix, Py, Iw, "Font Size", FormatFixedDecimals(Sel->FontSizePx, 0), "px", FsNorm, Theme, BodyInput.PrimaryPointer, DragFs))
                {
                    ActiveSliderId  = FsSid;
                    Sel->FontSizePx = std::round((14.0f + FsNorm * 146.0f) * 0.5f) * 2.0f;
                    Session->BumpRevision();
                }
                if (DragFs) ActiveSliderId = FsSid;
                Py += 52.0f;
            }

            if (Sel->Category != PatternStampLayerCategory::Folder)
            {
                if (Sel->Category != PatternStampLayerCategory::GradientWash)
                {
                    bool FillOn = Sel->FillEnabled;
                    if (ConstructSwitchControlRow(Surface, Ix, Py, Iw, "Fill Enabled", FillOn, Theme, BodyInput.PrimaryPointer))
                    {
                        Session->PushUndoSnapshot();
                        Sel->FillEnabled = FillOn;
                        Session->BumpRevision();
                        Session->RecomputeSummaryMetrics();
                    }
                    Py += 34.0f;
                }

                if (Sel->FillEnabled || Sel->Category == PatternStampLayerCategory::GradientWash)
                {
                    static constexpr const char* SourceOpts[4] { "Solid", "Linear Grad", "Radial Grad", "Point Field" };
                    uint32_t ChosenSrc = static_cast<uint32_t>(Sel->FillSource);
                    if (ConstructSegmentedCapsuleRow(
                            Surface, Ix, Py, Iw, "Fill Source", SourceOpts, 4u, static_cast<uint32_t>(Sel->FillSource), ChosenSrc, Theme, BodyInput.PrimaryPointer))
                    {
                        Sel->FillSource = static_cast<PatternStampFillSourceMode>(ChosenSrc);
                        if (Sel->FillSource == PatternStampFillSourceMode::PointField)
                        {
                            PatternStampWorkspaceSession::InitializeDefaultPointFieldPins(*Sel);
                        }
                        Session->BumpRevision();
                    }
                    Py += 50.0f;

                    if (Sel->FillSource == PatternStampFillSourceMode::Solid)
                    {
                        if (ConstructColourSwatchRow(
                                Surface, Ix, Py, Iw, "Fill Colour", FormatHexColor(Sel->FillTint), Sel->FillTint, Theme, BodyInput.PrimaryPointer, CapturedFillSwatch))
                        {
                            ActiveSwatchTarget = (ActiveSwatchTarget == SwatchTargetChannel::LayerFill)
                                ? SwatchTargetChannel::None
                                : SwatchTargetChannel::LayerFill;
                            ActiveSwatchAnchor = CapturedFillSwatch;
                            ActiveSheet        = InspectorSheetVariant::None;
                        }
                        Py += 38.0f;
                    }
                    else
                    {
                        const Rectangle2D GradBarR{ Ix, Py, Iw, 14.0f };
                        Surface.FillRectangle(GradBarR, Sel->FillTint, 7.0f);
                        Surface.FillRectangle({ GradBarR.X + GradBarR.Width * 0.5f, GradBarR.Y, GradBarR.Width * 0.5f, GradBarR.Height }, Sel->FillTintB, 7.0f);
                        Surface.StrokeRectangle(GradBarR, Theme.BorderStrong, 7.0f, 1.0f);
                        Py += 20.0f;
                        if (ConstructColourSwatchRow(
                                Surface, Ix, Py, Iw, "Gradient Tint A", FormatHexColor(Sel->FillTint), Sel->FillTint, Theme, BodyInput.PrimaryPointer, CapturedFillSwatch))
                        {
                            ActiveSwatchTarget = (ActiveSwatchTarget == SwatchTargetChannel::LayerFill)
                                ? SwatchTargetChannel::None
                                : SwatchTargetChannel::LayerFill;
                            ActiveSwatchAnchor = CapturedFillSwatch;
                            ActiveSheet        = InspectorSheetVariant::None;
                        }
                        Py += 36.0f;
                    }
                }

                {
                    const int32_t SwSid  = NextSliderId++;
                    bool&         DragSw = BeginSlider(SwSid);
                    float         SwNorm = std::clamp(Sel->StrokeWidthPx / 18.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(
                            Surface, Ix, Py, Iw, "Stroke Outline", FormatFixedDecimals(Sel->StrokeWidthPx, 1), "px", SwNorm, Theme, BodyInput.PrimaryPointer, DragSw))
                    {
                        ActiveSliderId     = SwSid;
                        Sel->StrokeWidthPx = std::round(SwNorm * 18.0f * 2.0f) * 0.5f;
                        Session->BumpRevision();
                    }
                    if (DragSw) ActiveSliderId = SwSid;
                    Py += 52.0f;
                }

                if (Sel->StrokeWidthPx > 0.01f || !Sel->FillEnabled)
                {
                    if (ConstructColourSwatchRow(
                            Surface, Ix, Py, Iw, "Stroke Colour", FormatHexColor(Sel->StrokeTint), Sel->StrokeTint, Theme, BodyInput.PrimaryPointer, CapturedStrokeSwatch))
                    {
                        ActiveSwatchTarget = (ActiveSwatchTarget == SwatchTargetChannel::LayerStroke)
                            ? SwatchTargetChannel::None
                            : SwatchTargetChannel::LayerStroke;
                        ActiveSwatchAnchor = CapturedStrokeSwatch;
                        ActiveSheet        = InspectorSheetVariant::None;
                    }
                    Py += 38.0f;
                }

                static constexpr const char* BlendOpts[6] { "Normal", "Multiply", "Screen", "Overlay", "Add", "Darken" };
                if (ConstructDropdownBarRow(
                        Surface, Ix, Py, Iw, "Blend Mode", BlendOpts[Sel->BlendMode % 6u], ActiveSheet == InspectorSheetVariant::BlendMode, Theme, BodyInput.PrimaryPointer, CapturedBlendBtn))
                {
                    ActiveSheet        = (ActiveSheet == InspectorSheetVariant::BlendMode) ? InspectorSheetVariant::None : InspectorSheetVariant::BlendMode;
                    ActiveSheetAnchor  = CapturedBlendBtn;
                    ActiveSwatchTarget = SwatchTargetChannel::None;
                    DropdownScrollY    = 0.0f;
                }
                Py += 48.0f;

                {
                    const int32_t OpSid  = NextSliderId++;
                    bool&         DragOp = BeginSlider(OpSid);
                    float         OpNorm = std::clamp(Sel->Opacity, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(
                            Surface, Ix, Py, Iw, "Layer Opacity", FormatFixedDecimals(Sel->Opacity, 2), "-", OpNorm, Theme, BodyInput.PrimaryPointer, DragOp))
                    {
                        ActiveSliderId = OpSid;
                        Sel->Opacity   = std::round(OpNorm * 50.0f) / 50.0f;
                    }
                    if (DragOp) ActiveSliderId = OpSid;
                    Py += 52.0f;
                }
                {
                    const int32_t CxSid  = NextSliderId++;
                    bool&         DragCx = BeginSlider(CxSid);
                    float         CxNorm = std::clamp(Sel->CenterX / 512.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(
                            Surface, Ix, Py, Iw, "Center X", FormatFixedDecimals(Sel->CenterX, 0), "px", CxNorm, Theme, BodyInput.PrimaryPointer, DragCx))
                    {
                        ActiveSliderId = CxSid;
                        Sel->CenterX   = std::round(CxNorm * 512.0f);
                    }
                    if (DragCx) ActiveSliderId = CxSid;
                    Py += 52.0f;
                }
                {
                    const int32_t CySid  = NextSliderId++;
                    bool&         DragCy = BeginSlider(CySid);
                    float         CyNorm = std::clamp(Sel->CenterY / 512.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(
                            Surface, Ix, Py, Iw, "Center Y", FormatFixedDecimals(Sel->CenterY, 0), "px", CyNorm, Theme, BodyInput.PrimaryPointer, DragCy))
                    {
                        ActiveSliderId = CySid;
                        Sel->CenterY   = std::round(CyNorm * 512.0f);
                    }
                    if (DragCy) ActiveSliderId = CySid;
                    Py += 52.0f;
                }
                {
                    const int32_t WSid  = NextSliderId++;
                    bool&         DragW = BeginSlider(WSid);
                    float         WNorm = std::clamp((Sel->WidthPx - 6.0f) / 506.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(
                            Surface, Ix, Py, Iw, "Width", FormatFixedDecimals(Sel->WidthPx, 0), "px", WNorm, Theme, BodyInput.PrimaryPointer, DragW))
                    {
                        ActiveSliderId    = WSid;
                        const float PrevW = std::max(6.0f, Sel->WidthPx);
                        const float NextW = std::round((6.0f + WNorm * 506.0f) * 0.5f) * 2.0f;
                        const float Ratio = NextW / PrevW;
                        Sel->WidthPx      = NextW;
                        for (auto& Sub : Sel->CubicSubpaths)
                        {
                            for (auto& Knot : Sub.Knots)
                            {
                                Knot.AnchorX    *= Ratio;
                                Knot.InHandleX  *= Ratio;
                                Knot.OutHandleX *= Ratio;
                            }
                        }
                        Session->BumpRevision();
                        Session->RecomputeSummaryMetrics();
                    }
                    if (DragW) ActiveSliderId = WSid;
                    Py += 52.0f;
                }
                {
                    const int32_t HSid  = NextSliderId++;
                    bool&         DragH = BeginSlider(HSid);
                    float         HNorm = std::clamp((Sel->HeightPx - 6.0f) / 506.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(
                            Surface, Ix, Py, Iw, "Height", FormatFixedDecimals(Sel->HeightPx, 0), "px", HNorm, Theme, BodyInput.PrimaryPointer, DragH))
                    {
                        ActiveSliderId    = HSid;
                        const float PrevH = std::max(6.0f, Sel->HeightPx);
                        const float NextH = std::round((6.0f + HNorm * 506.0f) * 0.5f) * 2.0f;
                        const float Ratio = NextH / PrevH;
                        Sel->HeightPx     = NextH;
                        for (auto& Sub : Sel->CubicSubpaths)
                        {
                            for (auto& Knot : Sub.Knots)
                            {
                                Knot.AnchorY    *= Ratio;
                                Knot.InHandleY  *= Ratio;
                                Knot.OutHandleY *= Ratio;
                            }
                        }
                        Session->BumpRevision();
                        Session->RecomputeSummaryMetrics();
                    }
                    if (DragH) ActiveSliderId = HSid;
                    Py += 52.0f;
                }
                {
                    const int32_t RSid  = NextSliderId++;
                    bool&         DragR = BeginSlider(RSid);
                    float         RNorm = std::clamp((Sel->RotationDeg + 180.0f) / 360.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(
                            Surface, Ix, Py, Iw, "Rotation", FormatFixedDecimals(Sel->RotationDeg, 0), "deg", RNorm, Theme, BodyInput.PrimaryPointer, DragR))
                    {
                        ActiveSliderId   = RSid;
                        Sel->RotationDeg = std::round(-180.0f + RNorm * 360.0f);
                    }
                    if (DragR) ActiveSliderId = RSid;
                }
            }
        }
        CursorY += PropSecR.Height + 10.0f;

        // =========================================================================
        // B. PBR SURFACE CHANNELS SECTION
        // =========================================================================
        if (Sel->Category != PatternStampLayerCategory::Folder)
        {
            const std::vector<PatternStampSurfaceChannelKey> AvailCh = Session->QueryAvailableChannelsForSelectedLayer();

            std::vector<RemovableChipEntry> ChEntries;
            ChEntries.reserve(Sel->Channels.size());
            float ChipX     = 0.0f;
            float ChipRowsH = 26.0f;
            for (const auto& B : Sel->Channels)
            {
                const auto& Desc = PatternStampWorkspaceSession::QueryChannelDescriptor(B.Key);
                ChEntries.push_back({ Desc.Label, Desc.ColorChannel ? B.Tint : ColorRgba{ 158u, 162u, 170u, 255u }, false });
                const float CW = std::min(Iw, 16.0f + WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, Desc.Label) + 18.0f);
                if (ChipX > 0.0f && ChipX + CW > Iw + 0.5f)
                {
                    ChipX = 0.0f;
                    ChipRowsH += 27.0f;
                }
                ChipX += CW + 5.0f;
            }
            const std::string AddChLabel = AvailCh.empty() ? std::string() : ("+ Channel (" + std::to_string(AvailCh.size()) + ")");
            if (!AddChLabel.empty())
            {
                const float AddW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, AddChLabel) + 28.0f;
                if (ChipX > 0.0f && ChipX + AddW > Iw + 0.5f)
                {
                    ChipRowsH += 27.0f;
                }
            }

            float ChBlocksH = Sel->Channels.empty() ? 38.0f : 0.0f;
            for (const auto& B : Sel->Channels)
            {
                const auto& Desc = PatternStampWorkspaceSession::QueryChannelDescriptor(B.Key);
                ChBlocksH += (Desc.ColorChannel ? 86.0f : 96.0f) + 8.0f;
            }

            const float PbrBodyH = PbrChannelsExpanded ? (8.0f + 48.0f + 16.0f + ChipRowsH + 8.0f + ChBlocksH + 6.0f) : 0.0f;
            const Rectangle2D PbrSecR{ LeftX, CursorY, InnerW, 38.0f + PbrBodyH };
            BeginSectionContainer(Surface, PbrSecR, Theme, 18.0f);

            const std::string PbrTag = std::to_string(Sel->Channels.size()) + " ACTIVE";
            if (ConstructSectionHeaderRow(
                    Surface,
                    { LeftX + 12.0f, CursorY + 11.0f, InnerW - 24.0f, 20.0f },
                    ControlIcon::MaterialSphere,
                    "PBR Surface Channels",
                    PbrTag,
                    PbrChannelsExpanded,
                    Theme,
                    BodyCursor,
                    BodyPressed))
            {
                PbrChannelsExpanded = !PbrChannelsExpanded;
            }

            if (PbrChannelsExpanded)
            {
                float Sy = CursorY + 42.0f;

                const auto& FinLabels = PatternStampWorkspaceSession::InspectFinishLabels();
                const std::size_t FIdx = static_cast<std::size_t>(Sel->FinishPreset) % FinLabels.size();
                if (ConstructDropdownBarRow(
                        Surface, Ix, Sy, Iw, "Surface Finish", FinLabels[FIdx], ActiveSheet == InspectorSheetVariant::SurfaceFinish, Theme, BodyInput.PrimaryPointer, CapturedFinishBtn))
                {
                    ActiveSheet        = (ActiveSheet == InspectorSheetVariant::SurfaceFinish) ? InspectorSheetVariant::None : InspectorSheetVariant::SurfaceFinish;
                    ActiveSheetAnchor  = CapturedFinishBtn;
                    ActiveSwatchTarget = SwatchTargetChannel::None;
                    DropdownScrollY    = 0.0f;
                }
                Sy += 48.0f;

                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Regular, { Ix, Sy }, 9.8f, Theme.TextMuted, "Active PBR Channels");
                Sy += 16.0f;

                const RemovableChipOutcome ChipOut = ConstructRemovableChannelChips(
                    Surface,
                    Ix,
                    Sy,
                    Iw,
                    ChEntries,
                    AddChLabel,
                    ActiveSheet == InspectorSheetVariant::AddChannel,
                    Theme,
                    BodyCursor,
                    BodyPressed);
                if (ChipOut.AddTriggerClicked)
                {
                    ActiveSheet        = (ActiveSheet == InspectorSheetVariant::AddChannel) ? InspectorSheetVariant::None : InspectorSheetVariant::AddChannel;
                    ActiveSheetAnchor  = ChipOut.AddTriggerBounds;
                    CapturedAddChBtn   = ChipOut.AddTriggerBounds;
                    ActiveSwatchTarget = SwatchTargetChannel::None;
                    DropdownScrollY    = 0.0f;
                }
                bool PendingRemoveCh = false;
                PatternStampSurfaceChannelKey RemoveChKey = PatternStampSurfaceChannelKey::Albedo;
                if (ChipOut.RemovedIndex >= 0 && static_cast<std::size_t>(ChipOut.RemovedIndex) < Sel->Channels.size())
                {
                    PendingRemoveCh = true;
                    RemoveChKey     = Sel->Channels[static_cast<std::size_t>(ChipOut.RemovedIndex)].Key;
                }
                Sy += ChipOut.ConsumedHeight + 8.0f;

                if (Sel->Channels.empty())
                {
                    WorkspaceAppearance::ConstructTruncatedText(
                        Surface, FontWeightClass::Regular, { Ix, Sy + 6.0f }, Iw, 9.5f, Theme.TextFaint, "No PBR channels active - add channels above.");
                }
                else
                {
                    const auto SnapshotCh = Sel->Channels;
                    for (const auto& ChSnap : SnapshotCh)
                    {
                        const auto& Desc  = PatternStampWorkspaceSession::QueryChannelDescriptor(ChSnap.Key);
                        const float BlockH = Desc.ColorChannel ? 86.0f : 96.0f;
                        const Rectangle2D ChBlockR{ Ix, Sy, Iw, BlockH };
                        Surface.FillRectangle(ChBlockR, Theme.WellSurface, 12.0f);
                        Surface.StrokeRectangle(ChBlockR, Theme.BorderSubtle, 12.0f, 1.0f);

                        Surface.FillCircle({ ChBlockR.X + 12.0f, ChBlockR.Y + 17.0f }, 3.8f, Desc.ColorChannel ? ChSnap.Tint : ColorRgba{ 158u, 162u, 170u, 255u });
                        WorkspaceAppearance::ConstructWeightedText(
                            Surface, FontWeightClass::SemiBold, { ChBlockR.X + 21.0f, ChBlockR.Y + 11.0f }, 10.2f, Theme.TextPrimary, Desc.Label);

                        const float LblW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 10.2f, Desc.Label);
                        const float SubW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.2f, Desc.Subtitle) + 12.0f;
                        const Rectangle2D SubTagR{ ChBlockR.X + 25.0f + LblW, ChBlockR.Y + 9.0f, SubW, 16.0f };
                        if (SubTagR.Right() < ChBlockR.Right() - 106.0f)
                        {
                            Surface.FillRectangle(SubTagR, Theme.SectionSurface, 8.0f);
                            Surface.StrokeRectangle(SubTagR, Theme.BorderSubtle, 8.0f, 1.0f);
                            WorkspaceAppearance::ConstructWeightedText(
                                Surface, FontWeightClass::Regular, { SubTagR.X + 6.0f, SubTagR.Y + 3.0f }, 8.2f, Theme.TextFaint, Desc.Subtitle);
                        }

                        const Rectangle2D RmBtnR{ ChBlockR.Right() - 26.0f, ChBlockR.Y + 7.0f, 18.0f, 20.0f };
                        const Rectangle2D EyeBtnR{ RmBtnR.X - 22.0f, ChBlockR.Y + 7.0f, 18.0f, 20.0f };
                        const std::string ReadStr = Desc.ColorChannel
                            ? FormatHexColor(ChSnap.Tint)
                            : FormatFixedDecimals(ChSnap.ScalarReading, 2);
                        const float ValW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.8f, ReadStr) + 12.0f;
                        const Rectangle2D ReadoutR{ EyeBtnR.X - ValW - 6.0f, ChBlockR.Y + 8.0f, ValW, 18.0f };
                        Surface.FillRectangle(ReadoutR, Theme.SectionSurface, 6.0f);
                        Surface.StrokeRectangle(ReadoutR, Theme.BorderSubtle, 6.0f, 1.0f);
                        WorkspaceAppearance::ConstructWeightedText(
                            Surface, FontWeightClass::Medium, { ReadoutR.X + 6.0f, ReadoutR.Y + 3.0f }, 8.8f, Theme.TextPrimary, ReadStr);

                        Surface.FillRectangle(EyeBtnR, EyeBtnR.Contains(BodyCursor) ? Theme.HoverSurface : Theme.SectionSurface, 6.0f);
                        VectorIconRepository::ConstructControl(
                            Surface, ControlIcon::EyeVisible, VectorIconPlacement{ EyeBtnR.X + 3.0f, EyeBtnR.Y + 4.0f, 11.0f, Theme.TextMuted, 1.6f });
                        if (BodyPressed && EyeBtnR.Contains(BodyCursor))
                        {
                            Session->ViewChannel = static_cast<PatternStampViewChannel>(3u + static_cast<uint32_t>(ChSnap.Key));
                        }

                        Surface.FillRectangle(RmBtnR, RmBtnR.Contains(BodyCursor) ? Theme.HoverSurface : Theme.SectionSurface, 6.0f);
                        VectorIconRepository::ConstructControl(
                            Surface, ControlIcon::CloseCross, VectorIconPlacement{ RmBtnR.X + 4.0f, RmBtnR.Y + 5.0f, 9.0f, RmBtnR.Contains(BodyCursor) ? Theme.TextPrimary : Theme.TextMuted, 1.6f });
                        if (BodyPressed && RmBtnR.Contains(BodyCursor))
                        {
                            PendingRemoveCh = true;
                            RemoveChKey     = ChSnap.Key;
                        }

                        if (auto* MutableCh = Session->FindChannelEntry(*Sel, ChSnap.Key))
                        {
                            if (Desc.ColorChannel)
                            {
                                Rectangle2D SwBtnR {};
                                const std::string FieldTitle = std::string(Desc.Label) + " Reading";
                                if (ConstructColourSwatchRow(
                                        Surface, ChBlockR.X + 10.0f, ChBlockR.Y + 42.0f, ChBlockR.Width - 20.0f, FieldTitle, FormatHexColor(MutableCh->Tint), MutableCh->Tint, Theme, BodyInput.PrimaryPointer, SwBtnR))
                                {
                                    ActiveSwatchTarget = (ActiveSwatchTarget == SwatchTargetChannel::PbrChannel && ActiveSwatchChannelKey == ChSnap.Key)
                                        ? SwatchTargetChannel::None
                                        : SwatchTargetChannel::PbrChannel;
                                    ActiveSwatchChannelKey = ChSnap.Key;
                                    ActiveSwatchAnchor     = SwBtnR;
                                    ActiveSheet            = InspectorSheetVariant::None;
                                }
                            }
                            else
                            {
                                const int32_t ChSid  = NextSliderId++;
                                bool&         DragCh = BeginSlider(ChSid);
                                const float   Span   = std::max(0.01f, Desc.MaxScalar - Desc.MinScalar);
                                float         Norm   = std::clamp((MutableCh->ScalarReading - Desc.MinScalar) / Span, 0.0f, 1.0f);
                                const std::string FieldTitle = std::string(Desc.Label) + " Reading";
                                if (ConstructSliderReadoutRow(
                                        Surface,
                                        ChBlockR.X + 10.0f,
                                        ChBlockR.Y + 38.0f,
                                        ChBlockR.Width - 20.0f,
                                        FieldTitle,
                                        FormatFixedDecimals(MutableCh->ScalarReading, 2),
                                        "-",
                                        Norm,
                                        Theme,
                                        BodyInput.PrimaryPointer,
                                        DragCh))
                                {
                                    ActiveSliderId           = ChSid;
                                    const float Raw          = Desc.MinScalar + Norm * Span;
                                    MutableCh->ScalarReading = std::round(Raw / Desc.StepScalar) * Desc.StepScalar;
                                }
                                if (DragCh) ActiveSliderId = ChSid;
                            }
                        }
                        Sy += BlockH + 8.0f;
                    }
                }

                if (PendingRemoveCh)
                {
                    Session->RemoveChannelFromSelectedLayer(RemoveChKey);
                }
            }
            CursorY += PbrSecR.Height + 10.0f;

            // =========================================================================
            // C. LAYER MASK SECTION
            // =========================================================================
            const float MaskBodyH = LayerMaskExpanded ? (Sel->Mask.Enabled ? 250.0f : 42.0f) : 0.0f;
            const Rectangle2D MaskSecR{ LeftX, CursorY, InnerW, 38.0f + MaskBodyH };
            BeginSectionContainer(Surface, MaskSecR, Theme, 18.0f);
            if (ConstructSectionHeaderRow(
                    Surface,
                    { LeftX + 12.0f, CursorY + 11.0f, InnerW - 24.0f, 20.0f },
                    ControlIcon::MaskCutout,
                    "Layer Mask",
                    Sel->Mask.Enabled ? "ON" : "OFF",
                    LayerMaskExpanded,
                    Theme,
                    BodyCursor,
                    BodyPressed))
            {
                LayerMaskExpanded = !LayerMaskExpanded;
            }

            if (LayerMaskExpanded)
            {
                float Ky = CursorY + 42.0f;
                bool MOn = Sel->Mask.Enabled;
                if (ConstructSwitchControlRow(Surface, Ix, Ky, Iw, "Enable Layer Mask", MOn, Theme, BodyInput.PrimaryPointer))
                {
                    Sel->Mask.Enabled = MOn;
                    Session->RecomputeSummaryMetrics();
                }
                Ky += 34.0f;

                if (Sel->Mask.Enabled)
                {
                    static constexpr const char* ModeOpts[3] { "Noise", "Radial", "Stripe" };
                    uint32_t PickedM = static_cast<uint32_t>(Sel->Mask.Mode);
                    if (ConstructSegmentedCapsuleRow(
                            Surface, Ix, Ky, Iw, "Mask Mode", ModeOpts, 3u, static_cast<uint32_t>(Sel->Mask.Mode), PickedM, Theme, BodyInput.PrimaryPointer))
                    {
                        Sel->Mask.Mode = static_cast<PatternStampMaskMode>(PickedM);
                    }
                    Ky += 50.0f;

                    const int32_t TSid = NextSliderId++;
                    bool&         DragT = BeginSlider(TSid);
                    float         TNorm = std::clamp(Sel->Mask.Threshold, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Ix, Ky, Iw, "Threshold", FormatFixedDecimals(Sel->Mask.Threshold, 2), "-", TNorm, Theme, BodyInput.PrimaryPointer, DragT))
                    {
                        ActiveSliderId      = TSid;
                        Sel->Mask.Threshold = TNorm;
                    }
                    if (DragT) ActiveSliderId = TSid;
                    Ky += 52.0f;

                    const int32_t SSid = NextSliderId++;
                    bool&         DragS = BeginSlider(SSid);
                    float         SNorm = std::clamp((Sel->Mask.Softness - 0.02f) / 0.78f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Ix, Ky, Iw, "Softness", FormatFixedDecimals(Sel->Mask.Softness, 2), "-", SNorm, Theme, BodyInput.PrimaryPointer, DragS))
                    {
                        ActiveSliderId     = SSid;
                        Sel->Mask.Softness = 0.02f + SNorm * 0.78f;
                    }
                    if (DragS) ActiveSliderId = SSid;
                    Ky += 52.0f;

                    const int32_t FSid = NextSliderId++;
                    bool&         DragF = BeginSlider(FSid);
                    float         FNorm = std::clamp((Sel->Mask.Frequency - 1.0f) / 15.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Ix, Ky, Iw, "Scale / Freq", FormatFixedDecimals(Sel->Mask.Frequency, 0), "-", FNorm, Theme, BodyInput.PrimaryPointer, DragF))
                    {
                        ActiveSliderId      = FSid;
                        Sel->Mask.Frequency = std::round(1.0f + FNorm * 15.0f);
                    }
                    if (DragF) ActiveSliderId = FSid;
                    Ky += 52.0f;

                    bool Inv = Sel->Mask.Invert;
                    if (ConstructSwitchControlRow(Surface, Ix, Ky, Iw, "Invert Mask", Inv, Theme, BodyInput.PrimaryPointer))
                    {
                        Sel->Mask.Invert = Inv;
                    }
                }
            }
            CursorY += MaskSecR.Height + 10.0f;
        }

        // =========================================================================
        // D. NON-DESTRUCTIVE MODIFIERS SECTION
        // =========================================================================
        const auto& ModNames = PatternStampWorkspaceSession::InspectModifierLabels();
        std::vector<std::string> AddModBufs;
        std::vector<const char*> AddModPtrs;
        AddModBufs.reserve(ModNames.size());
        AddModPtrs.reserve(ModNames.size());
        for (const char* MN : ModNames)
        {
            AddModBufs.push_back(std::string("+ ") + MN);
            AddModPtrs.push_back(AddModBufs.back().c_str());
        }

        float ModChipH = 26.0f;
        {
            float RowCursorX = 0.0f;
            for (const auto& Lbl : AddModBufs)
            {
                const float CW = std::min(Iw, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.8f, Lbl) + 26.0f);
                if (RowCursorX > 0.0f && RowCursorX + CW > Iw + 0.5f)
                {
                    RowCursorX = 0.0f;
                    ModChipH += 29.0f;
                }
                RowCursorX += CW + 6.0f;
            }
        }

        auto MeasureModifierBlockHeight = [](const PatternStampModifierSpec& M) -> float
        {
            switch (M.Category)
            {
                case PatternStampModifierCategory::NoiseScale:     return 38.0f + 48.0f * 2.0f + 52.0f * 3.0f + 38.0f + 8.0f;
                case PatternStampModifierCategory::GridArray:      return 38.0f + 50.0f + 52.0f * 2.0f + 8.0f;
                case PatternStampModifierCategory::RadialArray:    return 38.0f + 52.0f * 3.0f + 8.0f;
                case PatternStampModifierCategory::MirrorSymmetry: return 38.0f + 50.0f + 8.0f;
                case PatternStampModifierCategory::InsetEcho:      return 38.0f + 52.0f * 2.0f + 38.0f + 8.0f;
                case PatternStampModifierCategory::ScatterDropout: return 38.0f + 52.0f * 3.0f + 8.0f;
                case PatternStampModifierCategory::DomainWarp:     return 38.0f + 50.0f + 52.0f + 8.0f;
                case PatternStampModifierCategory::BevelEdgeWear:  return 38.0f + 52.0f + 8.0f;
                default:                                  return 56.0f;
            }
        };

        float ModBlocksTotalH = 0.0f;
        for (const auto& M : Sel->Modifiers)
        {
            ModBlocksTotalH += MeasureModifierBlockHeight(M) + 8.0f;
        }

        const float ModSecBodyH = ModifiersExpanded ? (8.0f + ModChipH + 10.0f + ModBlocksTotalH + 6.0f) : 0.0f;
        const Rectangle2D ModSecR{ LeftX, CursorY, InnerW, 38.0f + ModSecBodyH };
        BeginSectionContainer(Surface, ModSecR, Theme, 18.0f);

        const std::string ModTag = std::to_string(Sel->Modifiers.size()) + " ACTIVE";
        if (ConstructSectionHeaderRow(
                Surface,
                { LeftX + 12.0f, CursorY + 11.0f, InnerW - 24.0f, 20.0f },
                ControlIcon::ModifierStar,
                "Non-Destructive Modifiers",
                ModTag,
                ModifiersExpanded,
                Theme,
                BodyCursor,
                BodyPressed))
        {
            ModifiersExpanded = !ModifiersExpanded;
        }

        if (ModifiersExpanded)
        {
            float My = CursorY + 42.0f;
            const ChipStripOutcome ModChipPick = ConstructWrappedChipStrip(
                Surface, Ix, My, Iw, AddModPtrs, -1, Theme, BodyCursor, BodyPressed, 23.0f, 8.8f);
            if (ModChipPick.ClickedIndex >= 0 && static_cast<std::size_t>(ModChipPick.ClickedIndex) < static_cast<std::size_t>(PatternStampModifierCategory::Count))
            {
                Session->AddModifierToSelectedLayer(static_cast<PatternStampModifierCategory>(ModChipPick.ClickedIndex));
            }
            My += ModChipPick.ConsumedHeight + 10.0f;

            bool        PendingRemoveMod = false;
            std::size_t RemoveModIdx     = 0u;

            for (std::size_t Mi = 0u; Mi < Sel->Modifiers.size(); ++Mi)
            {
                PatternStampModifierSpec& M     = Sel->Modifiers[Mi];
                const float          BlockH = MeasureModifierBlockHeight(M);
                const Rectangle2D    ModBlockR{ Ix, My, Iw, BlockH };
                Surface.FillRectangle(ModBlockR, Theme.WellSurface, 12.0f);
                Surface.StrokeRectangle(ModBlockR, Theme.BorderSubtle, 12.0f, 1.0f);

                VectorIconRepository::ConstructControl(
                    Surface, QueryIconForModifier(M.Category), VectorIconPlacement{ ModBlockR.X + 10.0f, ModBlockR.Y + 10.0f, 13.0f, Theme.TextMuted, 1.6f });
                const std::size_t ModCatIdx = static_cast<std::size_t>(M.Category) % ModNames.size();
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::SemiBold, { ModBlockR.X + 28.0f, ModBlockR.Y + 10.0f }, 10.2f, Theme.TextPrimary, ModNames[ModCatIdx]);

                const Rectangle2D ModRmR{ ModBlockR.Right() - 26.0f, ModBlockR.Y + 7.0f, 18.0f, 20.0f };
                const Rectangle2D ModEyeR{ ModRmR.X - 22.0f, ModBlockR.Y + 7.0f, 18.0f, 20.0f };
                Surface.FillRectangle(ModEyeR, ModEyeR.Contains(BodyCursor) ? Theme.HoverSurface : Theme.SectionSurface, 6.0f);
                VectorIconRepository::ConstructControl(
                    Surface, M.Enabled ? ControlIcon::EyeVisible : ControlIcon::EyeHidden, VectorIconPlacement{ ModEyeR.X + 3.0f, ModEyeR.Y + 4.0f, 11.0f, M.Enabled ? Theme.HighlightFill : Theme.TextFaint, 1.6f });
                if (BodyPressed && ModEyeR.Contains(BodyCursor))
                {
                    M.Enabled = !M.Enabled;
                    Session->RecomputeSummaryMetrics();
                }

                Surface.FillRectangle(ModRmR, ModRmR.Contains(BodyCursor) ? Theme.HoverSurface : Theme.SectionSurface, 6.0f);
                VectorIconRepository::ConstructControl(
                    Surface, ControlIcon::CloseCross, VectorIconPlacement{ ModRmR.X + 4.0f, ModRmR.Y + 5.0f, 9.0f, ModRmR.Contains(BodyCursor) ? Theme.TextPrimary : Theme.TextMuted, 1.6f });
                if (BodyPressed && ModRmR.Contains(BodyCursor))
                {
                    PendingRemoveMod = true;
                    RemoveModIdx     = Mi;
                }

                float       By = ModBlockR.Y + 38.0f;
                const float Bx = ModBlockR.X + 10.0f;
                const float Bw = ModBlockR.Width - 20.0f;

                if (M.Category == PatternStampModifierCategory::NoiseScale)
                {
                    static constexpr const char* AlgoOpts[5] { "FBM (Multi-Octave)", "Perlin Gradient", "Worley Cellular", "Ridged Folds", "Radial Falloff" };
                    Rectangle2D AlgoBtn {};
                    if (ConstructDropdownBarRow(
                            Surface, Bx, By, Bw, "Noise Algorithm", AlgoOpts[M.NoiseAlgorithm % 5u], ActiveSheet == InspectorSheetVariant::ModifierNoiseAlgo && ActiveSheetModIdx == Mi, Theme, BodyInput.PrimaryPointer, AlgoBtn))
                    {
                        ActiveSheet        = (ActiveSheet == InspectorSheetVariant::ModifierNoiseAlgo && ActiveSheetModIdx == Mi) ? InspectorSheetVariant::None : InspectorSheetVariant::ModifierNoiseAlgo;
                        ActiveSheetModIdx  = Mi;
                        ActiveSheetAnchor  = AlgoBtn;
                        ActiveSwatchTarget = SwatchTargetChannel::None;
                        DropdownScrollY    = 0.0f;
                    }
                    By += 48.0f;

                    static constexpr const char* TargetOpts[4] { "Scale + Color", "Scale Only", "Scale + Opacity", "Scale + Relief" };
                    Rectangle2D TgtBtn {};
                    if (ConstructDropdownBarRow(
                            Surface, Bx, By, Bw, "Modulate Target", TargetOpts[M.ModulateTarget % 4u], ActiveSheet == InspectorSheetVariant::ModifierModTarget && ActiveSheetModIdx == Mi, Theme, BodyInput.PrimaryPointer, TgtBtn))
                    {
                        ActiveSheet        = (ActiveSheet == InspectorSheetVariant::ModifierModTarget && ActiveSheetModIdx == Mi) ? InspectorSheetVariant::None : InspectorSheetVariant::ModifierModTarget;
                        ActiveSheetModIdx  = Mi;
                        ActiveSheetAnchor  = TgtBtn;
                        ActiveSwatchTarget = SwatchTargetChannel::None;
                        DropdownScrollY    = 0.0f;
                    }
                    By += 48.0f;

                    {
                        const int32_t MinSid   = NextSliderId++;
                        bool&         DragMinS = BeginSlider(MinSid);
                        float         MinSNorm = std::clamp((M.MinScale - 0.05f) / 1.95f, 0.0f, 1.0f);
                        if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Min Scale", FormatFixedDecimals(M.MinScale, 2), "-", MinSNorm, Theme, BodyInput.PrimaryPointer, DragMinS))
                        {
                            ActiveSliderId = MinSid;
                            M.MinScale     = 0.05f + MinSNorm * 1.95f;
                            Session->RecomputeSummaryMetrics();
                        }
                        if (DragMinS) ActiveSliderId = MinSid;
                        By += 52.0f;
                    }
                    {
                        const int32_t MaxSid   = NextSliderId++;
                        bool&         DragMaxS = BeginSlider(MaxSid);
                        float         MaxSNorm = std::clamp((M.MaxScale - 0.20f) / 2.80f, 0.0f, 1.0f);
                        if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Max Scale", FormatFixedDecimals(M.MaxScale, 2), "-", MaxSNorm, Theme, BodyInput.PrimaryPointer, DragMaxS))
                        {
                            ActiveSliderId = MaxSid;
                            M.MaxScale     = 0.20f + MaxSNorm * 2.80f;
                            Session->RecomputeSummaryMetrics();
                        }
                        if (DragMaxS) ActiveSliderId = MaxSid;
                        By += 52.0f;
                    }
                    {
                        const int32_t FrSid    = NextSliderId++;
                        bool&         DragFreq = BeginSlider(FrSid);
                        float         FrNorm   = std::clamp((M.Frequency - 0.5f) / 13.5f, 0.0f, 1.0f);
                        if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Frequency", FormatFixedDecimals(M.Frequency, 2), "-", FrNorm, Theme, BodyInput.PrimaryPointer, DragFreq))
                        {
                            ActiveSliderId = FrSid;
                            M.Frequency    = 0.5f + FrNorm * 13.5f;
                        }
                        if (DragFreq) ActiveSliderId = FrSid;
                        By += 52.0f;
                    }

                    Rectangle2D ModSwR {};
                    if (ConstructColourSwatchRow(Surface, Bx, By, Bw, "Target Color B", FormatHexColor(M.TargetTintB), M.TargetTintB, Theme, BodyInput.PrimaryPointer, ModSwR))
                    {
                        ActiveSwatchTarget = (ActiveSwatchTarget == SwatchTargetChannel::ModifierTintB && ActiveSwatchModIdx == Mi) ? SwatchTargetChannel::None : SwatchTargetChannel::ModifierTintB;
                        ActiveSwatchModIdx = Mi;
                        ActiveSwatchAnchor = ModSwR;
                        ActiveSheet        = InspectorSheetVariant::None;
                    }
                }
                else if (M.Category == PatternStampModifierCategory::GridArray)
                {
                    static constexpr const char* GridOpts[4] { "Grid", "Brick", "Hex", "Drop" };
                    uint32_t PickG = M.GridLayout;
                    if (ConstructSegmentedCapsuleRow(Surface, Bx, By, Bw, "Grid Layout", GridOpts, 4u, M.GridLayout, PickG, Theme, BodyInput.PrimaryPointer))
                    {
                        M.GridLayout = static_cast<uint8_t>(PickG);
                    }
                    By += 50.0f;

                    const int32_t CSid  = NextSliderId++;
                    bool&         DragC = BeginSlider(CSid);
                    float         CNorm = std::clamp(static_cast<float>(M.Columns - 1) / 23.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Columns", std::to_string(M.Columns), "-", CNorm, Theme, BodyInput.PrimaryPointer, DragC))
                    {
                        ActiveSliderId = CSid;
                        M.Columns      = static_cast<int32_t>(std::lround(1.0f + CNorm * 23.0f));
                        Session->RecomputeSummaryMetrics();
                    }
                    if (DragC) ActiveSliderId = CSid;
                    By += 52.0f;

                    const int32_t RSid  = NextSliderId++;
                    bool&         DragR = BeginSlider(RSid);
                    float         RNorm = std::clamp(static_cast<float>(M.Rows - 1) / 23.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Rows", std::to_string(M.Rows), "-", RNorm, Theme, BodyInput.PrimaryPointer, DragR))
                    {
                        ActiveSliderId = RSid;
                        M.Rows         = static_cast<int32_t>(std::lround(1.0f + RNorm * 23.0f));
                        Session->RecomputeSummaryMetrics();
                    }
                    if (DragR) ActiveSliderId = RSid;
                }
                else if (M.Category == PatternStampModifierCategory::RadialArray)
                {
                    const int32_t NSid  = NextSliderId++;
                    bool&         DragN = BeginSlider(NSid);
                    float         NNorm = std::clamp(static_cast<float>(M.RadialCount - 3) / 29.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Count", std::to_string(M.RadialCount), "-", NNorm, Theme, BodyInput.PrimaryPointer, DragN))
                    {
                        ActiveSliderId = NSid;
                        M.RadialCount  = static_cast<int32_t>(std::lround(3.0f + NNorm * 29.0f));
                        Session->RecomputeSummaryMetrics();
                    }
                    if (DragN) ActiveSliderId = NSid;
                    By += 52.0f;

                    const int32_t RadSid  = NextSliderId++;
                    bool&         DragRad = BeginSlider(RadSid);
                    float         RadNorm = std::clamp((M.RadiusPx - 20.0f) / 220.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Radius", FormatFixedDecimals(M.RadiusPx, 0), "px", RadNorm, Theme, BodyInput.PrimaryPointer, DragRad))
                    {
                        ActiveSliderId = RadSid;
                        M.RadiusPx     = std::round(20.0f + RadNorm * 220.0f);
                    }
                    if (DragRad) ActiveSliderId = RadSid;
                    By += 52.0f;

                    const int32_t RngSid  = NextSliderId++;
                    bool&         DragRng = BeginSlider(RngSid);
                    float         RngNorm = std::clamp(static_cast<float>(M.Rings - 1) / 4.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Rings", std::to_string(M.Rings), "-", RngNorm, Theme, BodyInput.PrimaryPointer, DragRng))
                    {
                        ActiveSliderId = RngSid;
                        M.Rings        = static_cast<int32_t>(std::lround(1.0f + RngNorm * 4.0f));
                        Session->RecomputeSummaryMetrics();
                    }
                    if (DragRng) ActiveSliderId = RngSid;
                }
                else if (M.Category == PatternStampModifierCategory::MirrorSymmetry)
                {
                    static constexpr const char* MirOpts[4] { "X", "Y", "Quad", "Diag" };
                    uint32_t PickM = M.MirrorAxis;
                    if (ConstructSegmentedCapsuleRow(Surface, Bx, By, Bw, "Symmetry Axis", MirOpts, 4u, M.MirrorAxis, PickM, Theme, BodyInput.PrimaryPointer))
                    {
                        M.MirrorAxis = static_cast<uint8_t>(PickM);
                    }
                }
                else if (M.Category == PatternStampModifierCategory::InsetEcho)
                {
                    const int32_t CpSid  = NextSliderId++;
                    bool&         DragCp = BeginSlider(CpSid);
                    float         CpNorm = std::clamp(static_cast<float>(M.EchoCopies - 1) / 7.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Echo Copies", std::to_string(M.EchoCopies), "-", CpNorm, Theme, BodyInput.PrimaryPointer, DragCp))
                    {
                        ActiveSliderId = CpSid;
                        M.EchoCopies   = static_cast<int32_t>(std::lround(1.0f + CpNorm * 7.0f));
                    }
                    if (DragCp) ActiveSliderId = CpSid;
                    By += 52.0f;

                    const int32_t StSid  = NextSliderId++;
                    bool&         DragSt = BeginSlider(StSid);
                    float         StNorm = std::clamp((M.InsetStepPx - 8.0f) / 82.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Inset Step", FormatFixedDecimals(M.InsetStepPx, 0), "px", StNorm, Theme, BodyInput.PrimaryPointer, DragSt))
                    {
                        ActiveSliderId = StSid;
                        M.InsetStepPx  = std::round(8.0f + StNorm * 82.0f);
                    }
                    if (DragSt) ActiveSliderId = StSid;
                    By += 52.0f;

                    Rectangle2D UnusedR {};
                    ConstructColourSwatchRow(Surface, Bx, By, Bw, "Alt Color", FormatHexColor(M.AltTint), M.AltTint, Theme, BodyInput.PrimaryPointer, UnusedR);
                }
                else if (M.Category == PatternStampModifierCategory::ScatterDropout)
                {
                    const int32_t PSid  = NextSliderId++;
                    bool&         DragP = BeginSlider(PSid);
                    float         PNorm = std::clamp(M.PosJitterPx / 80.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Position Jitter", FormatFixedDecimals(M.PosJitterPx, 0), "px", PNorm, Theme, BodyInput.PrimaryPointer, DragP))
                    {
                        ActiveSliderId = PSid;
                        M.PosJitterPx  = std::round(PNorm * 80.0f);
                    }
                    if (DragP) ActiveSliderId = PSid;
                    By += 52.0f;

                    const int32_t RtSid  = NextSliderId++;
                    bool&         DragRt = BeginSlider(RtSid);
                    float         RtNorm = std::clamp(M.RotJitterDeg / 180.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Rotation Jitter", FormatFixedDecimals(M.RotJitterDeg, 0), "deg", RtNorm, Theme, BodyInput.PrimaryPointer, DragRt))
                    {
                        ActiveSliderId = RtSid;
                        M.RotJitterDeg = std::round(RtNorm * 180.0f);
                    }
                    if (DragRt) ActiveSliderId = RtSid;
                    By += 52.0f;

                    const int32_t DrSid  = NextSliderId++;
                    bool&         DragDr = BeginSlider(DrSid);
                    float         DrNorm = std::clamp(M.Dropout / 0.85f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Dropout", FormatFixedDecimals(M.Dropout, 2), "-", DrNorm, Theme, BodyInput.PrimaryPointer, DragDr))
                    {
                        ActiveSliderId = DrSid;
                        M.Dropout      = DrNorm * 0.85f;
                    }
                    if (DragDr) ActiveSliderId = DrSid;
                }
                else if (M.Category == PatternStampModifierCategory::DomainWarp)
                {
                    static constexpr const char* WarpOpts[4] { "Wave", "Swirl", "Curl", "Pinch" };
                    uint32_t PickW = M.WarpMode;
                    if (ConstructSegmentedCapsuleRow(Surface, Bx, By, Bw, "Warp Mode", WarpOpts, 4u, M.WarpMode, PickW, Theme, BodyInput.PrimaryPointer))
                    {
                        M.WarpMode = static_cast<uint8_t>(PickW);
                    }
                    By += 50.0f;

                    const int32_t AmpSid  = NextSliderId++;
                    bool&         DragAmp = BeginSlider(AmpSid);
                    float         AmpNorm = std::clamp(M.WarpAmplitude / 120.0f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Amplitude", FormatFixedDecimals(M.WarpAmplitude, 0), "px", AmpNorm, Theme, BodyInput.PrimaryPointer, DragAmp))
                    {
                        ActiveSliderId  = AmpSid;
                        M.WarpAmplitude = std::round(AmpNorm * 120.0f);
                    }
                    if (DragAmp) ActiveSliderId = AmpSid;
                }
                else if (M.Category == PatternStampModifierCategory::BevelEdgeWear)
                {
                    const int32_t CwSid  = NextSliderId++;
                    bool&         DragCw = BeginSlider(CwSid);
                    float         CwNorm = std::clamp(M.ChipWearAmount / 0.90f, 0.0f, 1.0f);
                    if (ConstructSliderReadoutRow(Surface, Bx, By, Bw, "Chip Wear", FormatFixedDecimals(M.ChipWearAmount, 2), "-", CwNorm, Theme, BodyInput.PrimaryPointer, DragCw))
                    {
                        ActiveSliderId   = CwSid;
                        M.ChipWearAmount = CwNorm * 0.90f;
                    }
                    if (DragCw) ActiveSliderId = CwSid;
                }

                My += BlockH + 8.0f;
            }

            if (PendingRemoveMod)
            {
                Session->RemoveModifierFromSelectedLayer(RemoveModIdx);
            }
        }
        CursorY += ModSecR.Height + 10.0f;
    }

    // =========================================================================
    // E. DOCUMENT GROUND & TILE SECTION
    // =========================================================================
    float GroundBodyH = 0.0f;
    if (GroundSectionExpanded)
    {
        GroundBodyH = 38.0f;
        if (Session->Document.GroundEnabled) GroundBodyH += 38.0f;
        if (PatternModeActive) GroundBodyH += 34.0f;
    }
    const Rectangle2D GroundSecR{ LeftX, CursorY, InnerW, 38.0f + GroundBodyH };
    BeginSectionContainer(Surface, GroundSecR, Theme, 18.0f);
    if (ConstructSectionHeaderRow(
            Surface,
            { LeftX + 12.0f, CursorY + 11.0f, InnerW - 24.0f, 20.0f },
            ControlIcon::PatternTile,
            "Document Ground & Tile",
            Session->Document.GroundEnabled ? "ON" : "OFF",
            GroundSectionExpanded,
            Theme,
            BodyCursor,
            BodyPressed))
    {
        GroundSectionExpanded = !GroundSectionExpanded;
    }

    if (GroundSectionExpanded)
    {
        float Gy = CursorY + 42.0f;
        bool GOn = Session->Document.GroundEnabled;
        if (ConstructSwitchControlRow(Surface, LeftX + 12.0f, Gy, InnerW - 24.0f, "Ground Fill Enabled", GOn, Theme, BodyInput.PrimaryPointer))
        {
            Session->Document.GroundEnabled = GOn;
            Session->RecomputeSummaryMetrics();
        }
        Gy += 34.0f;

        if (Session->Document.GroundEnabled)
        {
            if (ConstructColourSwatchRow(
                    Surface, LeftX + 12.0f, Gy, InnerW - 24.0f, "Ground Colour", FormatHexColor(Session->Document.GroundTintA), Session->Document.GroundTintA, Theme, BodyInput.PrimaryPointer, CapturedGroundSwatch))
            {
                ActiveSwatchTarget = (ActiveSwatchTarget == SwatchTargetChannel::GroundTint) ? SwatchTargetChannel::None : SwatchTargetChannel::GroundTint;
                ActiveSwatchAnchor = CapturedGroundSwatch;
                ActiveSheet        = InspectorSheetVariant::None;
            }
            Gy += 38.0f;
        }
        if (PatternModeActive)
        {
            bool BordersOn = Session->Document.ShowGhostTileBorders;
            if (ConstructSwitchControlRow(Surface, LeftX + 12.0f, Gy, InnerW - 24.0f, "Show Ghost Repeat Borders", BordersOn, Theme, BodyInput.PrimaryPointer))
            {
                Session->Document.ShowGhostTileBorders = BordersOn;
            }
        }
    }
    CursorY += GroundSecR.Height + 12.0f;

    CachedContentHeight = std::max(ScrollRect.Height, CursorY - ScrollTopY + 12.0f);
    ScrollOffsetY       = std::clamp(ScrollOffsetY, 0.0f, std::max(0.0f, CachedContentHeight - ScrollRect.Height));
    WorkspaceAppearance::ConstructVerticalScrollIndicator(Surface, ScrollRect, CachedContentHeight, ScrollOffsetY, BodyCursor);
    Surface.PopClip();

    // 5. Pane Footer
    const Rectangle2D FooterR{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    ConstructPaneStatusFooter(Surface, FooterR, Session->StatusNotice, "Live SVG + PBR", Theme, false);

    // 6. Overlay Sheets & Colour Swatch Board
    if (ActiveSheet == InspectorSheetVariant::MotifShape && Sel != nullptr)
    {
        const auto& ShapeLabels = PatternStampWorkspaceSession::InspectMotifShapeLabels();
        bool Open = true;
        const uint32_t CurIdx = static_cast<uint32_t>(Sel->MotifShape);
        RenderFittedDropdownSheet(
            Surface,
            ActiveSheetAnchor,
            Bounds,
            ShapeLabels.data(),
            static_cast<uint32_t>(ShapeLabels.size()),
            CurIdx,
            Open,
            Input,
            [this, Sel](uint32_t Picked)
            {
                const auto& Labels = PatternStampWorkspaceSession::InspectMotifShapeLabels();
                if (Picked < Labels.size())
                {
                    Sel->MotifShape = static_cast<PatternStampMotifShape>(Picked);
                    Sel->CubicSubpaths.clear();
                    PatternStampWorkspaceSession::SeedMotifCubicSubpathsIfEmpty(*Sel);
                    Session->BumpRevision();
                    Session->RecomputeSummaryMetrics();
                }
            },
            &DropdownScrollY);
        if (!Open) ActiveSheet = InspectorSheetVariant::None;
    }
    else if (ActiveSheet == InspectorSheetVariant::BlendMode && Sel != nullptr)
    {
        static constexpr const char* BlendOpts[6] { "Normal", "Multiply", "Screen", "Overlay", "Add", "Darken" };
        bool Open = true;
        RenderFittedDropdownSheet(
            Surface,
            ActiveSheetAnchor,
            Bounds,
            BlendOpts,
            6u,
            Sel->BlendMode,
            Open,
            Input,
            [Sel](uint32_t Picked)
            {
                if (Picked < 6u) Sel->BlendMode = static_cast<uint8_t>(Picked);
            },
            &DropdownScrollY);
        if (!Open) ActiveSheet = InspectorSheetVariant::None;
    }
    else if (ActiveSheet == InspectorSheetVariant::SurfaceFinish && Sel != nullptr)
    {
        const auto& FinLabels = PatternStampWorkspaceSession::InspectFinishLabels();
        bool Open = true;
        const uint32_t CurIdx = static_cast<uint32_t>(Sel->FinishPreset);
        RenderFittedDropdownSheet(
            Surface,
            ActiveSheetAnchor,
            Bounds,
            FinLabels.data(),
            static_cast<uint32_t>(FinLabels.size()),
            CurIdx,
            Open,
            Input,
            [this](uint32_t Picked)
            {
                const auto& Labels = PatternStampWorkspaceSession::InspectFinishLabels();
                if (Picked < Labels.size()) Session->ApplyFinishPresetToSelectedLayer(static_cast<PatternStampFinishPreset>(Picked));
            },
            &DropdownScrollY);
        if (!Open) ActiveSheet = InspectorSheetVariant::None;
    }
    else if (ActiveSheet == InspectorSheetVariant::AddChannel && Sel != nullptr)
    {
        const auto Avail = Session->QueryAvailableChannelsForSelectedLayer();
        std::vector<const char*> Labels;
        Labels.reserve(Avail.size());
        for (PatternStampSurfaceChannelKey K : Avail)
        {
            Labels.push_back(PatternStampWorkspaceSession::QueryChannelDescriptor(K).Label);
        }
        bool Open = true;
        RenderFittedDropdownSheet(
            Surface,
            ActiveSheetAnchor,
            Bounds,
            Labels.data(),
            static_cast<uint32_t>(Labels.size()),
            999u,
            Open,
            Input,
            [this, &Avail](uint32_t Picked)
            {
                if (Picked < Avail.size()) Session->AddChannelToSelectedLayer(Avail[Picked]);
            },
            &DropdownScrollY);
        if (!Open) ActiveSheet = InspectorSheetVariant::None;
    }
    else if (ActiveSheet == InspectorSheetVariant::ModifierNoiseAlgo && Sel != nullptr && ActiveSheetModIdx < Sel->Modifiers.size())
    {
        static constexpr const char* AlgoOpts[5] { "FBM (Multi-Octave)", "Perlin Gradient", "Worley Cellular", "Ridged Folds", "Radial Falloff" };
        bool Open = true;
        const std::size_t Idx = ActiveSheetModIdx;
        RenderFittedDropdownSheet(
            Surface,
            ActiveSheetAnchor,
            Bounds,
            AlgoOpts,
            5u,
            Sel->Modifiers[Idx].NoiseAlgorithm,
            Open,
            Input,
            [Sel, Idx](uint32_t Picked)
            {
                if (Picked < 5u) Sel->Modifiers[Idx].NoiseAlgorithm = static_cast<uint8_t>(Picked);
            },
            &DropdownScrollY);
        if (!Open) ActiveSheet = InspectorSheetVariant::None;
    }
    else if (ActiveSheet == InspectorSheetVariant::ModifierModTarget && Sel != nullptr && ActiveSheetModIdx < Sel->Modifiers.size())
    {
        static constexpr const char* TargetOpts[4] { "Scale + Color", "Scale Only", "Scale + Opacity", "Scale + Relief" };
        bool Open = true;
        const std::size_t Idx = ActiveSheetModIdx;
        RenderFittedDropdownSheet(
            Surface,
            ActiveSheetAnchor,
            Bounds,
            TargetOpts,
            4u,
            Sel->Modifiers[Idx].ModulateTarget,
            Open,
            Input,
            [Sel, Idx](uint32_t Picked)
            {
                if (Picked < 4u) Sel->Modifiers[Idx].ModulateTarget = static_cast<uint8_t>(Picked);
            },
            &DropdownScrollY);
        if (!Open) ActiveSheet = InspectorSheetVariant::None;
    }

    if (ActiveSwatchTarget != SwatchTargetChannel::None)
    {
        ColorRgba* EditTint = nullptr;
        if (ActiveSwatchTarget == SwatchTargetChannel::GroundTint)
        {
            EditTint = &Session->Document.GroundTintA;
        }
        else if (Sel != nullptr)
        {
            if (ActiveSwatchTarget == SwatchTargetChannel::LayerFill) EditTint = &Sel->FillTint;
            else if (ActiveSwatchTarget == SwatchTargetChannel::LayerStroke) EditTint = &Sel->StrokeTint;
            else if (ActiveSwatchTarget == SwatchTargetChannel::PbrChannel)
            {
                if (auto* Ch = Session->FindChannelEntry(*Sel, ActiveSwatchChannelKey)) EditTint = &Ch->Tint;
            }
            else if (ActiveSwatchTarget == SwatchTargetChannel::ModifierTintB && ActiveSwatchModIdx < Sel->Modifiers.size())
            {
                EditTint = &Sel->Modifiers[ActiveSwatchModIdx].TargetTintB;
            }
        }

        if (EditTint != nullptr)
        {
            const Rectangle2D BoardRect{
                std::clamp(ActiveSwatchAnchor.X, Bounds.X + 8.0f, std::max(Bounds.X + 8.0f, Bounds.Right() - 210.0f)),
                std::clamp(ActiveSwatchAnchor.Bottom() + 4.0f, Bounds.Y + 8.0f, std::max(Bounds.Y + 8.0f, Bounds.Bottom() - 126.0f)),
                200.0f,
                112.0f };
            Surface.FillRectangle(BoardRect, Theme.SectionSurface, 12.0f);
            Surface.StrokeRectangle(BoardRect, Theme.BorderStrong, 12.0f, 1.0f);

            bool PickedSwatch = false;
            const Point2D Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
            for (std::size_t Si = 0u; Si < PatternStampQuickSwatches.size(); ++Si)
            {
                const std::size_t Col = Si % 4u;
                const std::size_t Row = Si / 4u;
                const Rectangle2D Tile{ BoardRect.X + 10.0f + static_cast<float>(Col) * 46.0f, BoardRect.Y + 10.0f + static_cast<float>(Row) * 24.0f, 40.0f, 18.0f };
                Surface.FillRectangle(Tile, PatternStampQuickSwatches[Si], 5.0f);
                const bool Match = (PatternStampQuickSwatches[Si].Red == EditTint->Red && PatternStampQuickSwatches[Si].Green == EditTint->Green && PatternStampQuickSwatches[Si].Blue == EditTint->Blue);
                Surface.StrokeRectangle(Tile, Match ? Theme.HighlightFill : Theme.BorderSubtle, 5.0f, Match ? 2.0f : 1.0f);
                if (Input.PrimaryPointer.Pressed && Tile.Contains(Cur))
                {
                    *EditTint          = PatternStampQuickSwatches[Si];
                    PickedSwatch       = true;
                    ActiveSwatchTarget = SwatchTargetChannel::None;
                }
            }
            if (Input.PrimaryPointer.Pressed && !PickedSwatch && !BoardRect.Contains(Cur) && !ActiveSwatchAnchor.Contains(Cur))
            {
                ActiveSwatchTarget = SwatchTargetChannel::None;
            }
        }
        else
        {
            ActiveSwatchTarget = SwatchTargetChannel::None;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
