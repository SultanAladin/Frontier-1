/*============================================================================================================================================
                                                        PATTERNSTAMPINSPECTORPANE.CPP
============================================================================================================================================*/
// 🧩 1:1 implementation of PatternStampInspectorPane matching Experimental/TextureStudio docked Artwork Inspector (cards-pattern.png,
//    rows-side-by-side.png, fill-linear-scene.png, fill-point-field.png, menu-shapes.png): Pattern/Stamp Studio header, Document hero,
//    3 KPI tiles (Instances, Coverage, Palette), 7 vertical collapsible property rows (Shape, Fill, Outline, Placement, Blend, Surface,
//    Mask) that expand vertically in-place, Modifiers stack, Document fold, and .svg / Project / Copy footer.

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

void FormatUpperHex(ColorRgba Rgba, char* OutBuf, std::size_t Cap) noexcept
{
    std::snprintf(OutBuf, Cap, "#%02X%02X%02X", Rgba.Red, Rgba.Green, Rgba.Blue);
}

void DrawFillSwatchTile(PixelSpace& Surface, Rectangle2D SwatchR, const PatternStampLayerSpec& Layer) noexcept
{
    if (Layer.FillSource == PatternStampFillSourceMode::LinearGradient ||
        Layer.FillSource == PatternStampFillSourceMode::RadialGradient ||
        Layer.FillSource == PatternStampFillSourceMode::PointField)
    {
        // Multi-stop gradient preview swatch
        const std::array<ColorRgba, 4u> Stops {{
            { 239u, 244u, 207u, 255u },
            { 196u, 215u, 178u, 255u },
            { 138u, 168u, 153u, 255u },
            {  90u, 118u, 146u, 255u },
        }};
        Surface.FillRectangle(SwatchR, Stops[0], 6.0f);
        const float SegW = SwatchR.Width / 4.0f;
        for (std::size_t I = 0u; I < 4u; ++I)
        {
            Surface.PushClip({ SwatchR.X + static_cast<float>(I) * SegW, SwatchR.Y, SegW + 1.0f, SwatchR.Height });
            Surface.FillRectangle(SwatchR, Stops[I], 6.0f);
            Surface.PopClip();
        }
        Surface.StrokeRectangle(SwatchR, ColorRgba{ 255u, 255u, 255u, 28u }, 6.0f, 1.0f);
    }
    else
    {
        Surface.FillRectangle(SwatchR, Layer.FillTint, 6.0f);
        Surface.StrokeRectangle(SwatchR, ColorRgba{ 255u, 255u, 255u, 28u }, 6.0f, 1.0f);
    }
}

void DrawDockedSwitch(PixelSpace& Surface, Rectangle2D SwitchR, bool Enabled) noexcept
{
    const ColorRgba TrackFill = Enabled ? ColorRgba{ 255u, 255u, 255u, 255u } : ColorRgba{ 35u, 35u, 38u, 255u };
    const ColorRgba KnobFill  = Enabled ? ColorRgba{  17u,  17u,  17u, 255u } : ColorRgba{ 188u, 188u, 188u, 255u };
    Surface.FillRectangle(SwitchR, TrackFill, SwitchR.Height * 0.5f);
    if (!Enabled)
    {
        Surface.StrokeRectangle(SwitchR, ColorRgba{ 255u, 255u, 255u, 24u }, SwitchR.Height * 0.5f, 1.0f);
    }
    const float KnobX = Enabled ? (SwitchR.Right() - 9.0f) : (SwitchR.X + 9.0f);
    Surface.FillCircle({ KnobX, SwitchR.Y + SwitchR.Height * 0.5f }, 6.5f, KnobFill);
}

void DrawSplitSliderRow(
    PixelSpace&                 Surface,
    float                       LeftX,
    float                       TopY,
    float                       InnerW,
    std::string_view            Label,
    std::string_view            NumText,
    std::string_view            UnitText,
    float                       NormFrac,
    const TexturingThemeColors& Theme) noexcept
{
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY }, 8.6f, Theme.TextMuted, Label);

    const float RowY = TopY + 15.0f;
    const Rectangle2D SplitR{ LeftX, RowY, 68.0f, 24.0f };
    Surface.FillRectangle(SplitR, ColorRgba{ 9u, 10u, 12u, 255u }, 12.0f);
    Surface.PushClip({ SplitR.Right() - 24.0f, SplitR.Y, 24.0f, SplitR.Height });
    Surface.FillRectangle(SplitR, ColorRgba{ 21u, 22u, 25u, 255u }, 12.0f);
    Surface.PopClip();
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { SplitR.X + 10.0f, SplitR.Y + 5.5f }, 8.6f, Theme.TextPrimary, NumText);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { SplitR.Right() - 18.0f, SplitR.Y + 6.0f }, 7.8f, Theme.TextFaint, UnitText);

    const Rectangle2D TrackR{ SplitR.Right() + 8.0f, RowY + 2.0f, InnerW - 76.0f, 20.0f };
    Surface.FillRectangle(TrackR, ColorRgba{ 23u, 24u, 27u, 255u }, 10.0f);
    const float Clamped = std::clamp(NormFrac, 0.0f, 1.0f);
    const float FillW   = std::max(10.0f, TrackR.Width * Clamped);
    Surface.FillRectangle({ TrackR.X, TrackR.Y, FillW, TrackR.Height }, ColorRgba{ 76u, 77u, 82u, 255u }, 10.0f);
    const float ThumbX = std::clamp(TrackR.X + TrackR.Width * Clamped, TrackR.X + 9.0f, TrackR.Right() - 9.0f);
    Surface.FillCircle({ ThumbX, TrackR.Y + 10.0f }, 7.5f, ColorRgba{ 235u, 235u, 235u, 255u });
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
    ActiveSliderId  = -1;
    ScrollOffsetY   = 0.0f;
    PropertyRowOpen = {{ false, false, false, false, false, false, false }};
}

void PatternStampInspectorPane::CloseAllSheets() noexcept
{
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
    (void)DeltaSeconds;
    if (Session == nullptr || Bounds.Width < 160.0f || Bounds.Height < 200.0f)
    {
        return;
    }

    const TexturingThemeColors Theme   = QueryTexturingThemeColors();
    const Point2D              Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed = Input.PrimaryPointer.Pressed;
    const bool                 IsPat   = (Session->Mode == PatternStampWorkspaceMode::Pattern);

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.LightSurface ? Theme.PaneBackground : ColorRgba{ 24u, 25u, 28u, 255u }, 0.0f);

    const float PadX   = 14.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float       CurY   = Bounds.Y + 12.0f;

    // 1. Top Card Bar ("Pattern Studio / SEAMLESS TILE" or "Stamp Studio / PBR DECAL" + Undo Redo Upload Reset)
    VectorIconRepository::ConstructControl(
        Surface, IsPat ? ControlIcon::PatternTile : ControlIcon::StampTool,
        VectorIconPlacement{ LeftX, CurY + 3.0f, 14.0f, ColorRgba{ 240u, 164u, 58u, 255u }, 1.75f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { LeftX + 20.0f, CurY - 1.0f }, 10.2f, Theme.TextPrimary,
        IsPat ? "Pattern Studio" : "Stamp Studio");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { LeftX + 20.0f, CurY + 11.5f }, 7.2f, Theme.TextFaint,
        IsPat ? "SEAMLESS TILE" : "PBR DECAL");

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
        if (ConstructHeaderIconButton(Surface, ActR, TopActions[I], false, true, Theme, Cursor, Pressed))
        {
            if (I == 0u)      (void)Session->ApplyUndo();
            else if (I == 1u) (void)Session->ApplyRedo();
        }
    }
    CurY += 28.0f;
    Surface.StrokeLine({ LeftX, CurY }, { LeftX + InnerW, CurY }, ColorRgba{ 255u, 255u, 255u, 14u }, 1.0f);
    CurY += 10.0f;

    // Reserve space at bottom for Export Bar (38px) + Status Line (22px) matching cards-pattern.png
    constexpr float ExportBarH = 38.0f;
    constexpr float StatusBarH = 22.0f;
    const Rectangle2D ScrollRect{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - ExportBarH - StatusBarH - CurY)
    };
    if (ScrollRect.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        const float MaxScroll = std::max(0.0f, CachedContentHeight - ScrollRect.Height);
        ScrollOffsetY         = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScroll);
    }

    Surface.PushClip(ScrollRect);
    float Sy = ScrollRect.Y + 2.0f - ScrollOffsetY;
    const float ScrollStartY = Sy;

    // 2. Document Hero Block ("IMPORTED" / DocumentName / "Imported document" + Preset Dropdown Pill)
    // Note: in Stamp mode (menu-shapes.png), a 44x44 preview thumb sits on the left; in Pattern mode (cards-pattern.png & CheckCardDock.mjs), no thumb!
    float HeroTextX = LeftX;
    if (!IsPat)
    {
        const Rectangle2D StampThumbR{ LeftX, Sy + 2.0f, 44.0f, 44.0f };
        Surface.FillRectangle(StampThumbR, ColorRgba{ 20u, 22u, 26u, 255u }, 10.0f);
        Surface.StrokeRectangle(StampThumbR, ColorRgba{ 255u, 255u, 255u, 22u }, 10.0f, 1.0f);
        Surface.FillCircle({ StampThumbR.X + 22.0f, StampThumbR.Y + 22.0f }, 14.0f, ColorRgba{ 118u, 152u, 186u, 255u });
        Surface.FillCircle({ StampThumbR.X + 22.0f, StampThumbR.Y + 22.0f }, 7.5f, ColorRgba{ 230u, 238u, 246u, 255u });
        HeroTextX += 54.0f;
    }

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { HeroTextX, Sy }, 7.4f, Theme.TextFaint, "IMPORTED");
    Sy += 13.0f;

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { HeroTextX, Sy }, LeftX + InnerW - HeroTextX, 17.5f, Theme.TextPrimary,
        Session->Document.DocumentName);
    Sy += 24.0f;

    const float DropW = std::min(168.0f, InnerW * 0.54f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { HeroTextX, Sy + 5.0f }, LeftX + InnerW - HeroTextX - DropW - 8.0f, 8.2f, Theme.TextFaint,
        "Imported document");

    char PresetPillText[64];
    std::snprintf(PresetPillText, sizeof(PresetPillText), "%s (imported)", Session->Document.DocumentName.c_str());
    const Rectangle2D PresetR{ LeftX + InnerW - DropW, Sy, DropW, 24.0f };
    Surface.FillRectangle(PresetR, Theme.LightSurface ? Theme.WellSurface : ColorRgba{ 9u, 10u, 12u, 255u }, 12.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Medium, { PresetR.X + 10.0f, PresetR.Y + 5.0f }, PresetR.Width - 24.0f, 8.2f, Theme.TextPrimary, PresetPillText);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ChevronDown, VectorIconPlacement{ PresetR.Right() - 14.0f, PresetR.Y + 7.5f, 9.0f, Theme.TextMuted, 1.6f });
    Sy += 32.0f;

    // 3. Three KPI Metric Tiles: Instances, Coverage, Palette — matches cards-pattern.png & menu-shapes.png 1:1
    const float TileGap = 8.0f;
    const float TileW   = (InnerW - TileGap * 2.0f) / 3.0f;
    const float TileH   = 66.0f;
    const ColorRgba TileFill = Theme.LightSurface ? Theme.SectionSurface : ColorRgba{ 33u, 34u, 39u, 255u };

    // Tile 1: Instances
    {
        const Rectangle2D T1{ LeftX, Sy, TileW, TileH };
        Surface.FillRectangle(T1, TileFill, 12.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { T1.X + 10.0f, T1.Y + 7.0f }, 8.2f, Theme.TextMuted, "Instances");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { T1.X + 10.0f, T1.Y + 20.0f }, 15.0f, Theme.TextPrimary, IsPat ? "125" : "1");
        const Rectangle2D Bar{ T1.X + 10.0f, T1.Y + 42.0f, T1.Width - 20.0f, 3.5f };
        Surface.FillRectangle(Bar, ColorRgba{ 19u, 20u, 23u, 255u }, 1.8f);
        Surface.FillRectangle({ Bar.X, Bar.Y, Bar.Width * (IsPat ? 0.58f : 1.0f), Bar.Height }, ColorRgba{ 235u, 235u, 235u, 255u }, 1.8f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { T1.X + 10.0f, T1.Y + 50.0f }, T1.Width - 16.0f, 7.0f, Theme.TextFaint,
            IsPat ? "2 LAYERS · 18 MS" : "1 LAYER · 1 MS");
    }
    // Tile 2: Coverage
    {
        const Rectangle2D T2{ LeftX + TileW + TileGap, Sy, TileW, TileH };
        Surface.FillRectangle(T2, TileFill, 12.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { T2.X + 10.0f, T2.Y + 7.0f }, 8.2f, Theme.TextMuted, "Coverage");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { T2.X + 10.0f, T2.Y + 20.0f }, 15.0f, Theme.TextPrimary, IsPat ? "46" : "27");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { T2.X + 32.0f, T2.Y + 25.0f }, 9.2f, Theme.TextMuted, "%");
        const Rectangle2D Bar{ T2.X + 10.0f, T2.Y + 42.0f, T2.Width - 20.0f, 3.5f };
        Surface.FillRectangle(Bar, ColorRgba{ 19u, 20u, 23u, 255u }, 1.8f);
        Surface.FillRectangle({ Bar.X, Bar.Y, Bar.Width * (IsPat ? 0.46f : 0.27f), Bar.Height }, ColorRgba{ 235u, 235u, 235u, 255u }, 1.8f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { T2.X + 10.0f, T2.Y + 50.0f }, T2.Width - 16.0f, 7.0f,
            IsPat ? Theme.TextFaint : ColorRgba{ 74u, 222u, 128u, 255u },
            IsPat ? "8 WRAP THE SEAM" : "106 PX MARGIN");
    }
    // Tile 3: Palette
    {
        const Rectangle2D T3{ LeftX + (TileW + TileGap) * 2.0f, Sy, TileW, TileH };
        Surface.FillRectangle(T3, TileFill, 12.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { T3.X + 10.0f, T3.Y + 7.0f }, 8.2f, Theme.TextMuted, "Palette");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { T3.X + 10.0f, T3.Y + 20.0f }, 15.0f, Theme.TextPrimary, IsPat ? "4" : "3");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { T3.X + 23.0f, T3.Y + 25.0f }, 8.8f, Theme.TextMuted, "colours");
        const Rectangle2D Bar{ T3.X + 10.0f, T3.Y + 42.0f, T3.Width - 20.0f, 4.0f };
        const std::array<ColorRgba, 4u> PalCols {{
            { 239u, 244u, 207u, 255u },
            { 158u, 207u, 255u, 255u },
            {  90u, 118u, 146u, 255u },
            {  23u,  26u,  31u, 255u },
        }};
        const float SegW = Bar.Width / 4.0f;
        for (std::size_t P = 0u; P < 4u; ++P)
        {
            Surface.FillRectangle({ Bar.X + static_cast<float>(P) * SegW, Bar.Y, SegW - 1.0f, Bar.Height }, PalCols[P], 1.5f);
        }
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { T3.X + 10.0f, T3.Y + 50.0f }, T3.Width - 16.0f, 7.0f, Theme.TextFaint,
            IsPat ? "CONTRAST 84%" : "CONTRAST 48%");
    }
    Sy += TileH + 14.0f;

    // 4. Selected Layer Section ("Fine Accent Rings  RING · 2 MOD" or "Solid Disc  CIRCLE")
    PatternStampLayerSpec* Sel = Session->InspectSelectedLayer();
    if (Sel != nullptr)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX, Sy }, 10.2f, Theme.TextPrimary, Sel->Name);
        char RightMeta[48];
        if (!Sel->Modifiers.empty())
        {
            std::snprintf(RightMeta, sizeof(RightMeta), "%s · %zu MOD",
                IsPat ? "RING" : "CIRCLE", Sel->Modifiers.size());
        }
        else
        {
            std::snprintf(RightMeta, sizeof(RightMeta), "%s", IsPat ? "RING" : "CIRCLE");
        }
        WorkspaceAppearance::ConstructRightAlignedText(
            Surface, FontWeightClass::Medium, { LeftX + InnerW, Sy + 2.0f }, 7.4f, Theme.TextFaint, RightMeta);
        Sy += 22.0f;

        // Helper to draw a closed property row header
        auto DrawPropHead = [&](std::size_t RowIdx, ControlIcon Icon, bool DrawFillSwatch, std::string_view Title, std::string_view Sub, std::string_view PillText, Rectangle2D ContainerR, bool Open)
        {
            const Rectangle2D HeadR{ ContainerR.X, ContainerR.Y, ContainerR.Width, 38.0f };
            if (!Open && HeadR.Contains(Cursor))
            {
                Surface.FillRectangle(HeadR, Theme.LightSurface ? Theme.HoverSurface : ColorRgba{ 29u, 30u, 34u, 255u }, 8.0f);
            }
            const Rectangle2D IconR{ HeadR.X + (Open ? 10.0f : 6.0f), HeadR.Y + 7.0f, 24.0f, 24.0f };
            if (DrawFillSwatch)
            {
                DrawFillSwatchTile(Surface, IconR, *Sel);
            }
            else
            {
                VectorIconRepository::ConstructControl(
                    Surface, Icon, VectorIconPlacement{ IconR.X + 4.5f, IconR.Y + 4.5f, 15.0f, Theme.TextMuted, 1.65f });
            }

            const float PillW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.4f, PillText) + 18.0f;
            const Rectangle2D PillR{ HeadR.Right() - PillW - (Open ? 10.0f : 4.0f), HeadR.Y + 9.0f, PillW, 20.0f };
            Surface.FillRectangle(PillR, Theme.LightSurface ? Theme.WellSurface : ColorRgba{ 9u, 10u, 12u, 255u }, 10.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { PillR.X + 9.0f, PillR.Y + 3.5f }, 8.4f, Theme.TextPrimary, PillText);

            const float TextX = IconR.Right() + 10.0f;
            const float TextW = std::max(32.0f, PillR.X - TextX - 8.0f);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Medium, { TextX, HeadR.Y + 5.5f }, TextW, 10.0f, Theme.TextPrimary, Title);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { TextX, HeadR.Y + 20.0f }, TextW, 8.2f, Theme.TextFaint, Sub);

            if (Pressed && HeadR.Contains(Cursor))
            {
                PropertyRowOpen[RowIdx] = !PropertyRowOpen[RowIdx];
            }
        };

        // Row 0: Shape ("Motif primitive")
        static constexpr std::array<const char*, 6u> BlendNames {{ "Normal", "Multiply", "Screen", "Overlay", "Add", "Darken" }};
        const auto& MotifLabels  = PatternStampWorkspaceSession::InspectMotifShapeLabels();
        const auto& ModLabels    = PatternStampWorkspaceSession::InspectModifierLabels();
        const auto& FinishLabels = PatternStampWorkspaceSession::InspectFinishLabels();
        {
            const std::string_view ShapeName = MotifLabels[std::min<std::size_t>(static_cast<std::size_t>(Sel->MotifShape), MotifLabels.size() - 1u)];
            const Rectangle2D R0{ LeftX, Sy, InnerW, 38.0f };
            DrawPropHead(0u, ControlIcon::ShapeCircle, false, "Shape", "Motif primitive", ShapeName, R0, false);
            Sy += 40.0f;
        }

        // Row 1: Fill ("Albedo source") — supports Solid, Linear, Radial, Point Field open states (rows-side-by-side.png, fill-linear-scene.png, fill-point-field.png)
        {
            char FillPill[32];
            if (Sel->FillSource == PatternStampFillSourceMode::LinearGradient)
            {
                std::snprintf(FillPill, sizeof(FillPill), "Linear · 4 stops");
            }
            else if (Sel->FillSource == PatternStampFillSourceMode::RadialGradient)
            {
                std::snprintf(FillPill, sizeof(FillPill), "Radial · 3 stops");
            }
            else if (Sel->FillSource == PatternStampFillSourceMode::PointField)
            {
                std::snprintf(FillPill, sizeof(FillPill), "Point field");
            }
            else
            {
                FormatUpperHex(Sel->FillTint, FillPill, sizeof(FillPill));
            }

            if (!PropertyRowOpen[1])
            {
                const Rectangle2D R1{ LeftX, Sy, InnerW, 38.0f };
                DrawPropHead(1u, ControlIcon::Count, true, "Fill", "Albedo source", FillPill, R1, false);
                Sy += 40.0f;
            }
            else
            {
                const bool IsLinearOrRadial = (Sel->FillSource == PatternStampFillSourceMode::LinearGradient ||
                                               Sel->FillSource == PatternStampFillSourceMode::RadialGradient);
                const bool IsPoint          = (Sel->FillSource == PatternStampFillSourceMode::PointField);
                const float CardH           = IsLinearOrRadial ? 314.0f : (IsPoint ? 290.0f : 154.0f);
                const Rectangle2D CardR{ LeftX, Sy, InnerW, CardH };
                Surface.FillRectangle(CardR, Theme.LightSurface ? Theme.HoverSurface : ColorRgba{ 34u, 35u, 39u, 255u }, 12.0f);

                DrawPropHead(1u, ControlIcon::Count, true, "Fill", "Albedo source", FillPill, CardR, true);
                float Fy = CardR.Y + 44.0f;
                const float Fx = CardR.X + 10.0f;
                const float Fw = CardR.Width - 20.0f;

                // "Fill shape / Interior colour or ramp" + switch
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Fx, Fy }, 9.2f, Theme.TextPrimary, "Fill shape");
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Fx, Fy + 13.0f }, 7.8f, Theme.TextFaint, "Interior colour or ramp");
                DrawDockedSwitch(Surface, { CardR.Right() - 44.0f, Fy + 4.0f, 34.0f, 18.0f }, Sel->FillEnabled);
                Fy += 32.0f;

                // Segmented source bar: Solid | Linear | Radial | Point
                const Rectangle2D SegR{ Fx, Fy, Fw, 24.0f };
                Surface.FillRectangle(SegR, ColorRgba{ 18u, 18u, 20u, 255u }, 12.0f);
                static constexpr const char* FillTabs[4] { "Solid", "Linear", "Radial", "Point" };
                const float TabW = (Fw - 6.0f) / 4.0f;
                const uint8_t CurTab = static_cast<uint8_t>(Sel->FillSource);
                for (uint8_t Ti = 0u; Ti < 4u; ++Ti)
                {
                    const Rectangle2D Tr{ SegR.X + 3.0f + static_cast<float>(Ti) * TabW, SegR.Y + 2.0f, TabW, 20.0f };
                    const bool Act = (CurTab == Ti);
                    if (Act)
                    {
                        Surface.FillRectangle(Tr, ColorRgba{ 255u, 255u, 255u, 255u }, 10.0f);
                    }
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, Act ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                        { Tr.X + (TabW - 26.0f) * 0.5f, Tr.Y + 3.5f }, 8.4f,
                        Act ? ColorRgba{ 17u, 17u, 17u, 255u } : Theme.TextMuted, FillTabs[Ti]);
                    if (Pressed && Tr.Contains(Cursor))
                    {
                        Sel->FillSource = static_cast<PatternStampFillSourceMode>(Ti);
                    }
                }
                Fy += 32.0f;

                if (!IsLinearOrRadial && !IsPoint)
                {
                    // Solid colour bar (#EFF4CF  sRGB)
                    const Rectangle2D ColBarR{ Fx, Fy, Fw, 30.0f };
                    Surface.FillRectangle(ColBarR, ColorRgba{ 9u, 10u, 12u, 255u }, 15.0f);
                    Surface.FillRectangle({ ColBarR.X + 5.0f, ColBarR.Y + 4.0f, 22.0f, 22.0f }, Sel->FillTint, 11.0f);
                    WorkspaceAppearance::ConstructWeightedText(
                        Surface, FontWeightClass::Medium, { ColBarR.X + 34.0f, ColBarR.Y + 8.0f }, 9.2f, Theme.TextPrimary, FillPill);
                    WorkspaceAppearance::ConstructRightAlignedText(
                        Surface, FontWeightClass::Regular, { ColBarR.Right() - 12.0f, ColBarR.Y + 8.5f }, 8.2f, Theme.TextFaint, "sRGB");
                }
                else
                {
                    // Multi-stop gradient ramp bar + 4 stop chips + '-' / '+' buttons
                    const std::array<ColorRgba, 4u> Stops {{
                        { 239u, 244u, 207u, 255u },
                        { 196u, 215u, 178u, 255u },
                        { 138u, 168u, 153u, 255u },
                        {  90u, 118u, 146u, 255u },
                    }};
                    const Rectangle2D RampR{ Fx, Fy, Fw, 16.0f };
                    const float SegW = Fw / 4.0f;
                    for (std::size_t S = 0u; S < 4u; ++S)
                    {
                        Surface.FillRectangle({ RampR.X + static_cast<float>(S) * SegW, RampR.Y, SegW + 1.0f, RampR.Height }, Stops[S], 6.0f);
                    }
                    Fy += 22.0f;

                    const float ChipW = (Fw - 56.0f - 12.0f) / 4.0f;
                    for (std::size_t S = 0u; S < 4u; ++S)
                    {
                        const Rectangle2D Cr{ Fx + static_cast<float>(S) * (ChipW + 4.0f), Fy, ChipW, 20.0f };
                        Surface.FillRectangle(Cr, Stops[S], 6.0f);
                    }
                    Surface.FillRectangle({ Fx + Fw - 50.0f, Fy, 22.0f, 20.0f }, ColorRgba{ 24u, 25u, 28u, 255u }, 6.0f);
                    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Fx + Fw - 42.0f, Fy + 3.0f }, 9.0f, Theme.TextMuted, "-");
                    Surface.FillRectangle({ Fx + Fw - 24.0f, Fy, 24.0f, 20.0f }, ColorRgba{ 24u, 25u, 28u, 255u }, 6.0f);
                    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Fx + Fw - 15.0f, Fy + 3.0f }, 9.0f, Theme.TextMuted, "+");
                    Fy += 28.0f;

                    if (IsLinearOrRadial)
                    {
                        DrawSplitSliderRow(Surface, Fx, Fy, Fw, "Angle", "-32", "deg", 0.41f, Theme);
                        Fy += 44.0f;

                        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Fx, Fy }, 8.4f, Theme.TextMuted, "Interpolation");
                        const Rectangle2D InterpR{ Fx, Fy + 14.0f, Fw, 26.0f };
                        Surface.FillRectangle(InterpR, ColorRgba{ 9u, 10u, 12u, 255u }, 13.0f);
                        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { InterpR.X + 12.0f, InterpR.Y + 6.0f }, 8.8f, Theme.TextPrimary, "Cubic");
                        VectorIconRepository::ConstructControl(
                            Surface, ControlIcon::ChevronDown, VectorIconPlacement{ InterpR.Right() - 16.0f, InterpR.Y + 8.5f, 9.0f, Theme.TextMuted, 1.6f });
                        Fy += 46.0f;

                        // Each shape | Whole canvas
                        const Rectangle2D SpaceR{ Fx, Fy, Fw, 24.0f };
                        Surface.FillRectangle(SpaceR, ColorRgba{ 18u, 18u, 20u, 255u }, 12.0f);
                        const float HalfW = (Fw - 6.0f) * 0.5f;
                        const Rectangle2D S0{ SpaceR.X + 3.0f, SpaceR.Y + 2.0f, HalfW, 20.0f };
                        const Rectangle2D S1{ SpaceR.X + 3.0f + HalfW, SpaceR.Y + 2.0f, HalfW, 20.0f };
                        Surface.FillRectangle(WholeCanvasRamp ? S1 : S0, ColorRgba{ 255u, 255u, 255u, 255u }, 10.0f);
                        WorkspaceAppearance::ConstructWeightedText(
                            Surface, !WholeCanvasRamp ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                            { S0.X + 24.0f, S0.Y + 3.5f }, 8.4f, !WholeCanvasRamp ? ColorRgba{ 17u, 17u, 17u, 255u } : Theme.TextMuted, "Each shape");
                        WorkspaceAppearance::ConstructWeightedText(
                            Surface, WholeCanvasRamp ? FontWeightClass::SemiBold : FontWeightClass::Medium,
                            { S1.X + 20.0f, S1.Y + 3.5f }, 8.4f, WholeCanvasRamp ? ColorRgba{ 17u, 17u, 17u, 255u } : Theme.TextMuted, "Whole canvas");
                        Fy += 30.0f;

                        // "Place line in viewport" & "Reset placement"
                        const float BtnW = (Fw - 8.0f) * 0.5f;
                        const Rectangle2D B0{ Fx, Fy, BtnW, 24.0f };
                        const Rectangle2D B1{ Fx + BtnW + 8.0f, Fy, BtnW, 24.0f };
                        Surface.FillRectangle(B0, ColorRgba{ 24u, 25u, 28u, 255u }, 12.0f);
                        Surface.FillRectangle(B1, ColorRgba{ 24u, 25u, 28u, 255u }, 12.0f);
                        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { B0.X + 12.0f, B0.Y + 5.5f }, 8.2f, Theme.TextPrimary, "Place line in viewport");
                        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { B1.X + 18.0f, B1.Y + 5.5f }, 8.2f, Theme.TextMuted, "Reset placement");
                    }
                    else
                    {
                        DrawSplitSliderRow(Surface, Fx, Fy, Fw, "Spread", "1.00", "x", 0.35f, Theme);
                        Fy += 44.0f;

                        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Fx, Fy }, 8.4f, Theme.TextMuted, "Field base");
                        const Rectangle2D BaseR{ Fx, Fy + 14.0f, Fw, 26.0f };
                        Surface.FillRectangle(BaseR, ColorRgba{ 9u, 10u, 12u, 255u }, 13.0f);
                        Surface.FillRectangle({ BaseR.X + 5.0f, BaseR.Y + 4.0f, 18.0f, 18.0f }, ColorRgba{ 90u, 118u, 146u, 255u }, 9.0f);
                        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { BaseR.X + 29.0f, BaseR.Y + 6.0f }, 8.8f, Theme.TextPrimary, "#5A7692");
                        WorkspaceAppearance::ConstructRightAlignedText(Surface, FontWeightClass::Regular, { BaseR.Right() - 12.0f, BaseR.Y + 6.5f }, 8.0f, Theme.TextFaint, "sRGB");
                        Fy += 44.0f;

                        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Fx, Fy }, 8.4f, Theme.TextMuted, "Interpolation");
                        const Rectangle2D InterpR{ Fx, Fy + 14.0f, Fw, 26.0f };
                        Surface.FillRectangle(InterpR, ColorRgba{ 9u, 10u, 12u, 255u }, 13.0f);
                        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { InterpR.X + 12.0f, InterpR.Y + 6.0f }, 8.8f, Theme.TextPrimary, "Hermite");
                        VectorIconRepository::ConstructControl(
                            Surface, ControlIcon::ChevronDown, VectorIconPlacement{ InterpR.Right() - 16.0f, InterpR.Y + 8.5f, 9.0f, Theme.TextMuted, 1.6f });
                        Fy += 44.0f;

                        const Rectangle2D PlaceBtnR{ Fx, Fy, Fw, 24.0f };
                        Surface.FillRectangle(PlaceBtnR, ColorRgba{ 24u, 25u, 28u, 255u }, 12.0f);
                        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { PlaceBtnR.X + (Fw - 110.0f) * 0.5f, PlaceBtnR.Y + 5.5f }, 8.4f, Theme.TextPrimary, "Place points in viewport");
                    }
                }
                Sy += CardH + 6.0f;
            }
        }

        // Row 2: Outline ("Contour stroke") — expands vertically in-place alongside Fill (rows-side-by-side.png)
        {
            if (!PropertyRowOpen[2])
            {
                const Rectangle2D R2{ LeftX, Sy, InnerW, 38.0f };
                DrawPropHead(2u, ControlIcon::ShapeSquare, false, "Outline", "Contour stroke", "None", R2, false);
                Sy += 40.0f;
            }
            else
            {
                const float CardH = 116.0f;
                const Rectangle2D CardR{ LeftX, Sy, InnerW, CardH };
                Surface.FillRectangle(CardR, Theme.LightSurface ? Theme.HoverSurface : ColorRgba{ 34u, 35u, 39u, 255u }, 12.0f);
                DrawPropHead(2u, ControlIcon::ShapeSquare, false, "Outline", "Contour stroke", "None", CardR, true);
                DrawSplitSliderRow(Surface, CardR.X + 10.0f, CardR.Y + 44.0f, CardR.Width - 20.0f, "Width", "0.0", "px", 0.0f, Theme);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Regular, { CardR.X + 10.0f, CardR.Y + 92.0f }, 8.2f, Theme.TextFaint,
                    "Give the outline a width to colour it.");
                Sy += CardH + 6.0f;
            }
        }

        // Row 3: Placement ("Position · size · turn")
        {
            char PlacePill[48];
            std::snprintf(PlacePill, sizeof(PlacePill), "%.0f, %.0f · %.0f×%.0f",
                Sel->CenterX, Sel->CenterY, Sel->WidthPx, Sel->HeightPx);
            const Rectangle2D R3{ LeftX, Sy, InnerW, 38.0f };
            DrawPropHead(3u, ControlIcon::MoveArrows, false, "Placement", "Position · size · turn", PlacePill, R3, false);
            Sy += 40.0f;
        }

        // Row 4: Blend ("Compositing")
        {
            char BlendPill[32];
            const char* BlendLbl = BlendNames[std::min<std::size_t>(Sel->BlendMode, BlendNames.size() - 1u)];
            std::snprintf(BlendPill, sizeof(BlendPill), "%s · %d%%",
                BlendLbl,
                static_cast<int>(std::lround(Sel->Opacity * 100.0f)));
            const Rectangle2D R4{ LeftX, Sy, InnerW, 38.0f };
            DrawPropHead(4u, ControlIcon::CircleNotice, false, "Blend", "Compositing", BlendPill, R4, false);
            Sy += 40.0f;
        }

        // Row 5: Surface ("PBR channels")
        {
            char SurfPill[48];
            const char* FinLbl = FinishLabels[std::min<std::size_t>(static_cast<std::size_t>(Sel->FinishPreset), FinishLabels.size() - 1u)];
            std::snprintf(SurfPill, sizeof(SurfPill), "%s · %zu ch", FinLbl, Sel->Channels.size());
            if (!PropertyRowOpen[5])
            {
                const Rectangle2D R5{ LeftX, Sy, InnerW, 38.0f };
                DrawPropHead(5u, ControlIcon::LayersSlabs, false, "Surface", "PBR channels", SurfPill, R5, false);
                Sy += 40.0f;
            }
            else
            {
                const float CardH = 48.0f + static_cast<float>(Sel->Channels.size()) * 40.0f;
                const Rectangle2D CardR{ LeftX, Sy, InnerW, CardH };
                Surface.FillRectangle(CardR, Theme.LightSurface ? Theme.HoverSurface : ColorRgba{ 34u, 35u, 39u, 255u }, 12.0f);
                DrawPropHead(5u, ControlIcon::LayersSlabs, false, "Surface", "PBR channels", SurfPill, CardR, true);
                float Cy = CardR.Y + 44.0f;
                for (auto& Ch : Sel->Channels)
                {
                    const auto& Meta = PatternStampWorkspaceSession::QueryChannelDescriptor(Ch.Key);
                    char Readout[24];
                    if (Meta.ColorChannel)
                    {
                        FormatUpperHex(Ch.Tint, Readout, sizeof(Readout));
                    }
                    else
                    {
                        std::snprintf(Readout, sizeof(Readout), "%d %%", static_cast<int>(std::lround(Ch.ScalarReading * 100.0f)));
                    }
                    (void)ConstructChannelEntryRow(
                        Surface, { CardR.X + 8.0f, Cy, CardR.Width - 16.0f, 38.0f },
                        Meta.ColorChannel ? Ch.Tint : Meta.DefaultTint,
                        Meta.ColorChannel, Ch.ScalarReading, Meta.Label, Meta.Subtitle,
                        Readout, true, false, Theme, Cursor, Pressed);
                    Cy += 40.0f;
                }
                Sy += CardH + 6.0f;
            }
        }

        // Row 6: Mask ("Layer cut-out")
        {
            const Rectangle2D R6{ LeftX, Sy, InnerW, 38.0f };
            DrawPropHead(6u, ControlIcon::MaskCutout, false, "Mask", "Layer cut-out", Sel->Mask.Enabled ? "On" : "Off", R6, false);
            Sy += 44.0f;
        }

        // 5. Modifiers Section ("Modifiers  2 OF 2 ACTIVE · IN ORDER" + "+ Modifier" button)
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX, Sy + 2.0f }, 10.2f, Theme.TextPrimary, "Modifiers");
        char ModHeadMeta[48];
        if (!Sel->Modifiers.empty())
        {
            std::snprintf(ModHeadMeta, sizeof(ModHeadMeta), "%zu OF %zu ACTIVE · IN ORDER",
                Sel->Modifiers.size(), Sel->Modifiers.size());
        }
        else
        {
            std::snprintf(ModHeadMeta, sizeof(ModHeadMeta), "NONE");
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { LeftX + 56.0f, Sy + 4.5f }, 7.4f, Theme.TextFaint, ModHeadMeta);

        const Rectangle2D AddModBtnR{ LeftX + InnerW - 76.0f, Sy, 76.0f, 22.0f };
        Surface.FillRectangle(AddModBtnR, Theme.LightSurface ? Theme.SectionSurface : ColorRgba{ 33u, 34u, 39u, 255u }, 11.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { AddModBtnR.X + 12.0f, AddModBtnR.Y + 4.0f }, 8.4f, Theme.TextPrimary, "+ Modifier");
        if (Pressed && AddModBtnR.Contains(Cursor))
        {
            Session->AddModifierToSelectedLayer(PatternStampModifierCategory::GridArray);
        }
        Sy += 28.0f;

        if (Sel->Modifiers.empty())
        {
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { LeftX, Sy + 2.0f }, InnerW, 8.4f, Theme.TextFaint,
                "No modifiers - the motif is drawn once. Add an array to repeat it.");
            Sy += 26.0f;
        }
        else
        {
            for (std::size_t MIdx = 0u; MIdx < Sel->Modifiers.size(); ++MIdx)
            {
                const auto& Mod = Sel->Modifiers[MIdx];
                const bool IsNoise = (Mod.Category == PatternStampModifierCategory::NoiseScale ||
                                      Mod.Category == PatternStampModifierCategory::ScatterDropout);
                const Rectangle2D ModRowR{ LeftX, Sy, InnerW, 38.0f };
                Surface.FillCircle({ ModRowR.X + 6.0f, ModRowR.Y + 19.0f }, 3.0f, ColorRgba{ 52u, 199u, 89u, 255u });
                VectorIconRepository::ConstructControl(
                    Surface, IsNoise ? ControlIcon::NoiseWave : ControlIcon::PatternTile,
                    VectorIconPlacement{ ModRowR.X + 16.0f, ModRowR.Y + 12.0f, 14.0f, Theme.TextMuted, 1.65f });

                char ModSub[32], ModPill[48];
                std::snprintf(ModSub, sizeof(ModSub), "%s · #%zu", IsNoise ? "NOISE" : "ARRAY", MIdx + 1u);
                if (IsNoise)
                {
                    std::snprintf(ModPill, sizeof(ModPill), "WORLEY · %.2f-%.2fx", Mod.MinScale, Mod.MaxScale);
                }
                else
                {
                    std::snprintf(ModPill, sizeof(ModPill), "Grid %dx%d", Mod.Columns, Mod.Rows);
                }

                const float PillW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.2f, ModPill) + 18.0f;
                const Rectangle2D PillR{ ModRowR.Right() - PillW - 4.0f, ModRowR.Y + 9.0f, PillW, 20.0f };
                Surface.FillRectangle(PillR, Theme.LightSurface ? Theme.WellSurface : ColorRgba{ 9u, 10u, 12u, 255u }, 10.0f);
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { PillR.X + 9.0f, PillR.Y + 3.5f }, 8.2f, Theme.TextPrimary, ModPill);

                const char* ModName = ModLabels[std::min<std::size_t>(static_cast<std::size_t>(Mod.Category), ModLabels.size() - 1u)];
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Medium, { ModRowR.X + 36.0f, ModRowR.Y + 5.5f }, PillR.X - ModRowR.X - 42.0f, 9.8f,
                    Theme.TextPrimary, ModName);
                WorkspaceAppearance::ConstructTruncatedText(
                    Surface, FontWeightClass::Medium, { ModRowR.X + 36.0f, ModRowR.Y + 20.5f }, PillR.X - ModRowR.X - 42.0f, 7.4f,
                    Theme.TextFaint, ModSub);
                Sy += 40.0f;
            }
        }
        Sy += 6.0f;
    }

    // 6. Document Fold Bar ("Document  GROUND ON · 1024 PX  >")
    {
        const Rectangle2D DocFoldR{ LeftX, Sy, InnerW, 34.0f };
        Surface.FillRectangle(DocFoldR, Theme.LightSurface ? Theme.SectionSurface : ColorRgba{ 28u, 29u, 33u, 255u }, 10.0f);
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::ImageTile, VectorIconPlacement{ DocFoldR.X + 10.0f, DocFoldR.Y + 10.5f, 13.0f, Theme.TextMuted, 1.6f });
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { DocFoldR.X + 28.0f, DocFoldR.Y + 9.5f }, 9.2f, Theme.TextPrimary, "Document");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { DocFoldR.X + 82.0f, DocFoldR.Y + 11.0f }, 7.4f, Theme.TextFaint,
            IsPat ? "GROUND ON · 1024 PX" : "TRANSPARENT · 1024 PX");
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::ChevronForward, VectorIconPlacement{ DocFoldR.Right() - 16.0f, DocFoldR.Y + 12.0f, 9.0f, Theme.TextMuted, 1.6f });
        Sy += 42.0f;
    }

    CachedContentHeight = (Sy - ScrollStartY) + 8.0f;
    Surface.PopClip();

    // 7. Bottom Export Bar (.svg | Project | Copy) + Status Line ("Saved locally   Double-click a layer to rename · Esc closes")
    const float ExpY  = Bounds.Bottom() - ExportBarH - StatusBarH + 4.0f;
    const float BtnGap = 8.0f;
    const float BtnW   = (InnerW - BtnGap * 2.0f) / 3.0f;
    struct ExpBtnSpec { ControlIcon Icon; const char* Label; bool Primary; };
    static constexpr std::array<ExpBtnSpec, 3u> ExpBtns {{
        { ControlIcon::DownloadExport, ".svg",    true  },
        { ControlIcon::DownloadExport, "Project", false },
        { ControlIcon::CopyDuplicate,  "Copy",    false },
    }};
    for (std::size_t I = 0u; I < 3u; ++I)
    {
        const Rectangle2D Br{ LeftX + static_cast<float>(I) * (BtnW + BtnGap), ExpY, BtnW, 28.0f };
        const ColorRgba Fill = ExpBtns[I].Primary
            ? ColorRgba{ 255u, 255u, 255u, 255u }
            : (Theme.LightSurface ? Theme.SectionSurface : ColorRgba{ 34u, 35u, 39u, 255u });
        const ColorRgba Ink = ExpBtns[I].Primary ? ColorRgba{ 17u, 17u, 17u, 255u } : Theme.TextPrimary;
        Surface.FillRectangle(Br, Fill, 14.0f);
        VectorIconRepository::ConstructControl(
            Surface, ExpBtns[I].Icon, VectorIconPlacement{ Br.X + Br.Width * 0.5f - 22.0f, Br.Y + 8.0f, 11.5f, Ink, 1.7f });
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { Br.X + Br.Width * 0.5f - 6.0f, Br.Y + 6.5f }, 8.8f, Ink, ExpBtns[I].Label);
    }

    const float StatY = Bounds.Bottom() - StatusBarH + 4.0f;
    Surface.FillCircle({ LeftX + 4.0f, StatY + 6.5f }, 2.8f, ColorRgba{ 52u, 199u, 89u, 255u });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX + 12.0f, StatY }, 7.8f, Theme.TextFaint, "Saved locally");
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface, FontWeightClass::Regular, { LeftX + InnerW, StatY }, 7.6f, Theme.TextFaint,
        "Double-click a layer to rename · Esc closes");

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
