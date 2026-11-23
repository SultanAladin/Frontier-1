/*==========================================================================================================================================
                                                          APPEARANCEINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 Native theme and typography layouts adapted without renderer settings.

#include "AppearanceInspector.h"
#include "TypefaceRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

constexpr ColorQuad Hex(uint32_t Rgb, float Alpha = 1.0f) noexcept
{
    return ColorQuad{((Rgb >> 16) & 0xFFu) / 255.0f, ((Rgb >> 8) & 0xFFu) / 255.0f, (Rgb & 0xFFu) / 255.0f, Alpha};
}

// Notch dark theme text colours (colors.text / colors.textMuted) used on section headings.
// Notch colour classes resolved against the applied theme each frame (ControlKit::Palette()).
inline ColorQuad Ink90() noexcept
{
    return ControlKit::Palette().Text;
} // colors.text
inline ColorQuad Ink50() noexcept
{
    return ControlKit::Palette().TextDim;
} // colors.textMuted
inline ColorQuad Ink10() noexcept
{
    return ControlKit::Palette().LightSurface ? ColorQuad{0.0f, 0.0f, 0.0f, 0.10f} : ColorQuad{1.0f, 1.0f, 1.0f, 0.10f};
} // ring-white/10, colors.activeBg
inline ColorQuad Ink20() noexcept
{
    return ControlKit::Palette().LightSurface ? ColorQuad{0.0f, 0.0f, 0.0f, 0.20f} : ColorQuad{1.0f, 1.0f, 1.0f, 0.20f};
} // hover:ring-white/20

// Theme tile miniature palettes — Notch ThemeTab.tsx (bg / sidebar / panel / lines).
struct TilePalette
{
    const char *Name;
    ColorQuad   Background, Sidebar, Panel, Lines;
    bool        OutlinePanel;
};
constexpr TilePalette Tiles[8] = {
    {"OLED", Hex(0x000000), Hex(0x0A0A0A), Hex(0x141414), Hex(0xFFFFFF), false},
    {"Dark", Hex(0x18181A), Hex(0x222224), Hex(0x2C2C2E), Hex(0xFFFFFF), false},
    {"Light", Hex(0xE5E5E5), Hex(0xF4F4F5), Hex(0xFFFFFF), Hex(0x000000), true},
    {"Sepia", Hex(0xDCA85B), Hex(0xE6C697), Hex(0xF1DAB0), Hex(0x825619), false},
    {"Dracula", Hex(0x282A36), Hex(0x44475A), Hex(0x6272A4), Hex(0xF8F8F2), false},
    {"Nord", Hex(0x2E3440), Hex(0x3B4252), Hex(0x4C566A), Hex(0xECEFF4), false},
    {"GitHub", Hex(0x0D1117), Hex(0x161B22), Hex(0x21262D), Hex(0xC9D1D9), false},
    {"Dim", Hex(0x0F172A), Hex(0x1E293B), Hex(0x334155), Hex(0xE2E8F0), false},
};
constexpr ThemeCategory TileThemes[8] = {ThemeCategory::Oled,   ThemeCategory::Dark,    ThemeCategory::Light,
                                         ThemeCategory::Sepia,  ThemeCategory::Dracula, ThemeCategory::Nord,
                                         ThemeCategory::GitHub, ThemeCategory::Dim};

constexpr uint32_t AccentHex[10] = {0xFFFFFF, 0xF97316, 0xF59E0B, 0x84CC16, 0x10B981,
                                    0x06B6D4, 0x3B82F6, 0x8B5CF6, 0xD946EF, 0xF43F5E};

// Semantic swatch rows — Notch ThemeTab.tsx; Caution row added per direction (flagged).
constexpr uint32_t SemanticHex[4][4] = {
    {0xF59E0B, 0xEAB308, 0xFBBF24, 0xF97316}, // Warning
    {0x10B981, 0x22C55E, 0x34D399, 0x059669}, // Success
    {0x3B82F6, 0x0EA5E9, 0x60A5FA, 0x2563EB}, // Info
    {0xEAB308, 0xFACC15, 0xFDE047, 0xCA8A04}, // Caution (yellow ladder)
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

AppearanceInspector::AppearanceInspector() noexcept
    : Applied{}, Draft{}, Revision(0u), OpenDropdown(-1), DraggingSlider(-1),
      Dropdowns{DropdownRecord{{}, nullptr, 0u, 0u, -1}, DropdownRecord{{}, nullptr, 0u, 0u, -1},
                DropdownRecord{{}, nullptr, 0u, 0u, -1}, DropdownRecord{{}, nullptr, 0u, 0u, -1}},
      DropdownCount(0u), TileExtents{}, AccentExtents{}, RadiusSliderExtent{}, ScaleSliderExtent{}, DropdownExtents{},
      StripScroll(0.0f), StripScrollTarget(0.0f), StripContentWidth(0.0f), StripViewWidth(0.0f), FontCardExtents{},
      StripBackExtent{}, StripForwardExtent{}, RoleSliderExtents{}, ChipExtents{}
{
}

const char *AppearanceInspector::QueryThemeName(ThemeCategory Theme) noexcept
{
    for (uint32_t I = 0u; I < 8u; ++I)
        if (TileThemes[I] == Theme)
            return Tiles[I].Name;
    return "Dim";
}

ColorQuad AppearanceInspector::QueryAccentColour(AccentCategory Accent) noexcept
{
    return Hex(AccentHex[std::min<uint32_t>(static_cast<uint32_t>(Accent), 9u)]);
}

ColorQuad AppearanceInspector::QuerySemanticColour(uint32_t Row, uint32_t Swatch) noexcept
{
    return Hex(SemanticHex[std::min(Row, 3u)][std::min(Swatch, 3u)]);
}

AppearanceDifference AppearanceInspector::QueryDifference() const noexcept
{
    AppearanceDifference D{};
    auto                 Note = [&](bool Changed, const char *Name) {
        if (Changed)
        {
            if (D.Count < 8u)
                D.Names[D.Count] = Name;
            ++D.Count;
        }
    };
    Note(Draft.InterfaceScale != Applied.InterfaceScale, "UI scale");
    Note(Draft.Theme != Applied.Theme, "Theme");
    Note(Draft.CornerRadius != Applied.CornerRadius, "Corner radius");
    Note(Draft.Accent != Applied.Accent, "Accent");
    Note(Draft.WarningSwatch != Applied.WarningSwatch, "Warning colour");
    Note(Draft.SuccessSwatch != Applied.SuccessSwatch, "Success colour");
    Note(Draft.InfoSwatch != Applied.InfoSwatch, "Info colour");
    Note(Draft.CautionSwatch != Applied.CautionSwatch, "Caution colour");
    Note(Draft.FontFamily != Applied.FontFamily, "Font family");
    bool Sizes = false, Weights = false;
    for (uint32_t R = 0u; R < AppearanceSettings::TypeRoleCount; ++R)
    {
        Sizes |= Draft.RoleSize[R] != Applied.RoleSize[R];
        Weights |= Draft.RoleWeight[R] != Applied.RoleWeight[R];
    }
    Note(Sizes, "Type scale");
    Note(Weights, "Font weights");
    return D;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DROPDOWNS
//------------------------------------------------------------------------------------------------------------------------

void AppearanceInspector::RecordDropdown(PixelSpace &Surface, const PlaneExtent &Extent, int Ordinal,
                                         const char *const *Options, uint32_t Count, uint32_t &Value,
                                         const ControlPointer &Pointer, float Opacity) noexcept
{
    // While a menu is open every other control is inert (the floating layer owns the pointer).
    ControlPointer Local = Pointer;
    if (OpenDropdown >= 0)
        Local.Enabled = false;
    // A pick made in the floating layer (drawn after this tab last frame) lands here, one frame later.
    if (static_cast<uint32_t>(Ordinal) < 4u && Dropdowns[Ordinal].Pick >= 0)
    {
        Value                   = static_cast<uint32_t>(Dropdowns[Ordinal].Pick);
        Dropdowns[Ordinal].Pick = -1;
    }
    const ControlHit Hit = ControlKit::Dropdown(Surface, Extent, Options[std::min(Value, Count - 1u)],
                                                OpenDropdown == Ordinal, Local, Opacity);
    if (Hit.Clicked)
        OpenDropdown = Ordinal;
    if (static_cast<uint32_t>(Ordinal) < 4u)
    {
        Dropdowns[Ordinal]       = DropdownRecord{Extent, Options, Count, Value, -1};
        DropdownExtents[Ordinal] = Extent;
    }
    DropdownCount = std::max(DropdownCount, static_cast<uint32_t>(Ordinal + 1));
}

void AppearanceInspector::ConstructFloatingLayout(PixelSpace &Surface, const ControlPointer &Pointer,
                                                  float Opacity) noexcept
{
    if (OpenDropdown < 0 || static_cast<uint32_t>(OpenDropdown) >= DropdownCount)
        return;
    DropdownRecord  &R      = Dropdowns[OpenDropdown];
    uint32_t         Chosen = R.Value;
    const ControlHit Hit =
        ControlKit::DropdownMenu(Surface, R.Button, R.Options, R.Count, R.Value, Pointer, Chosen, Opacity);
    if (Hit.Clicked)
    {
        R.Pick       = static_cast<int>(Chosen);
        R.Value      = Chosen;
        OpenDropdown = -1;
        return;
    }
    // Release outside the menu and its button closes it.
    if (Pointer.Released && !Hit.Hovered && !R.Button.Encloses(Pointer.X, Pointer.Y))
        OpenDropdown = -1;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DISPLAY TAB
//------------------------------------------------------------------------------------------------------------------------

void AppearanceInspector::RecordThemeTile(PixelSpace &Surface, const PlaneExtent &Extent, ThemeCategory Theme,
                                          bool Active, float Radius, const ControlPointer &Pointer,
                                          float Opacity) noexcept
{
    uint32_t Index = 0u;
    for (uint32_t I = 0u; I < 8u; ++I)
        if (TileThemes[I] == Theme)
            Index = I;
    const TilePalette &T     = Tiles[Index];
    const bool         Hover = ControlKit::Over(Extent, Pointer);

    // aspect-[4/3] rounded-[1.25rem] (20 px) bg; ring-1 white/10 idle (hover white/20)
    Surface.FillRectangle(Extent, ControlKit::Faded(T.Background, Opacity), 20.0f);
    if (!Active)
        ControlKit::OutlineRounded(Surface, Extent, ControlKit::Faded(Hover ? Ink20() : Ink10(), Opacity), 20.0f);

    // Inner mock: sidebar from (24, 24) to the tile's bottom-right; 30 % sidebar column + panel.
    const PlaneExtent Inner =
        PlaneExtent{Extent.MinimumX + 24.0f, Extent.MinimumY + 24.0f, Extent.MaximumX, Extent.MaximumY};
    Surface.FillRectangle(Inner, ControlKit::Faded(T.Sidebar, Opacity), 0.0f);
    const float       SideW = Inner.Width() * 0.30f;
    const PlaneExtent Side  = PlaneExtent{Inner.MinimumX, Inner.MinimumY, Inner.MinimumX + SideW, Inner.MaximumY};
    const PlaneExtent Pane  = PlaneExtent{Side.MaximumX, Inner.MinimumY, Inner.MaximumX, Inner.MaximumY};
    Surface.FillRectangle(Pane, ControlKit::Faded(T.Panel, Opacity), 0.0f);
    if (T.OutlinePanel)
        ControlKit::OutlineRounded(Surface, Pane, ControlKit::Faded(ColorQuad{0.0f, 0.0f, 0.0f, 0.05f}, Opacity), 0.0f);
    Surface.FillRectangle(Spanning(Side.MaximumX, Side.MinimumY, 1.0f, Side.Height()),
                          ControlKit::Faded(ColorQuad{0.0f, 0.0f, 0.0f, 0.05f}, Opacity));

    const float LineR = Radius * 0.25f;
    auto        Line  = [&](float LX, float LY, float LW, float LH, float Alpha, float R) {
        Surface.FillRectangle(Spanning(LX, LY, LW, LH),
                                      ControlKit::Faded(ColorQuad{T.Lines.Red, T.Lines.Green, T.Lines.Blue, Alpha}, Opacity),
                                      R);
    };
    // Sidebar p-3: three 6 px dots, then 3 lines (full, 2/3, 1/2), then avatar row at the bottom.
    float SX = Side.MinimumX + 12.0f, SY = Side.MinimumY + 12.0f;
    for (int I = 0; I < 3; ++I)
        Line(SX + I * 10.0f, SY, 6.0f, 6.0f, 0.4f, 3.0f);
    SY += 6.0f + 8.0f;
    const float SW = Side.Width() - 24.0f;
    Line(SX, SY, SW, 6.0f, 0.2f, LineR);
    SY += 12.0f;
    Line(SX, SY, SW * 2.0f / 3.0f, 6.0f, 0.2f, LineR);
    SY += 12.0f;
    Line(SX, SY, SW * 0.5f, 6.0f, 0.2f, LineR);
    const float AY = Side.MaximumY - 12.0f - 10.0f;
    if (AY > SY + 8.0f)
    {
        Line(SX, AY, 10.0f, 10.0f, 0.4f, 5.0f);
        Line(SX + 16.0f, AY + 2.0f, 16.0f, 6.0f, 0.2f, LineR);
    }
    // Panel p-4: 1/4 line, 1/3 line, three squares, 1/5 line at the bottom.
    float       PX = Pane.MinimumX + 16.0f, PY = Pane.MinimumY + 16.0f;
    const float PW = Pane.Width() - 32.0f;
    Line(PX, PY, PW * 0.25f, 6.0f, 0.3f, LineR);
    PY += 6.0f + 4.0f + 6.0f;
    Line(PX, PY, PW / 3.0f, 6.0f, 0.2f, LineR);
    PY += 6.0f + 6.0f + 4.0f;
    const float Sq = std::max((PW - 16.0f) / 3.0f, 4.0f);
    for (int I = 0; I < 3; ++I)
        Line(PX + I * (Sq + 8.0f), PY, Sq, Sq, 0.1f, Radius * 0.5f);
    const float BY = Pane.MaximumY - 16.0f - 6.0f;
    if (BY > PY + Sq + 4.0f)
        Line(PX, BY, PW * 0.2f, 6.0f, 0.2f, LineR);
}

void AppearanceInspector::RecordSemanticRow(PixelSpace &Surface, float X, float Y, float Width, uint32_t Row,
                                            const char *Label, uint32_t &Swatch, ControlCentreIconCategory Icon,
                                            float Radius, bool Last, const ControlPointer &Pointer,
                                            float Opacity) noexcept
{
    // grid-cols-[300px_1fr] gap-8 items-center; left: label + 4 swatches; right: preview card min-h 100.
    const float      LeftW    = 300.0f;
    const float      PreviewH = 100.0f;
    const PlanePoint M        = Surface.MeasureText(Label, 14.0f);
    const float      LeftH    = 20.0f + 24.0f + ControlKit::SwatchDiameter;
    const float      RowH     = std::max(PreviewH, LeftH);
    const float      LeftY    = Y + (RowH - LeftH) * 0.5f;
    Surface.Text(X, LeftY + (20.0f - M.Y) * 0.5f, ControlKit::Faded(Ink90(), Opacity), Label, 14.0f);
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        const float Cx = X + 16.0f + I * (ControlKit::SwatchDiameter + 12.0f), Cy = LeftY + 20.0f + 24.0f + 16.0f;
        if (ControlKit::Swatch(Surface, Cx, Cy, QuerySemanticColour(Row, I), Swatch == I, false, Pointer, Opacity)
                .Clicked)
            Swatch = I;
    }
    const PlaneExtent Preview = Spanning(X + LeftW + 32.0f, Y, Width - LeftW - 32.0f, PreviewH);
    Surface.FillRectangle(Preview, ControlKit::Faded(Ink10(), Opacity), Radius * 0.7f); // colors.activeBg
    ControlKit::OutlineRounded(Surface, Preview, ControlKit::Faded(ControlKit::Palette().Stroke, Opacity),
                               Radius * 0.7f);
    const ColorQuad Tone = QuerySemanticColour(Row, Swatch);
    ControlKit::Glyph(Surface, Preview.MinimumX + 24.0f, Preview.MinimumY + (PreviewH - 20.0f) * 0.5f, 20.0f,
                      ControlKit::Faded(Tone, Opacity), Icon);
    const char      *Message = Row == 0u   ? "Warning Message"
                               : Row == 1u ? "Success Message"
                               : Row == 2u ? "Info Message"
                                           : "Caution Message";
    const PlanePoint MM      = Surface.MeasureText(Message, 18.0f);
    Surface.Text(Preview.MinimumX + 24.0f + 20.0f + 8.0f, Preview.MinimumY + (PreviewH - MM.Y) * 0.5f,
                 ControlKit::Faded(Tone, Opacity), Message, 18.0f);
    if (!Last)
        ControlKit::Divider(Surface, X, Y + RowH + 32.0f, Width,
                            ControlKit::Faded(ColorQuad{1.0f, 1.0f, 1.0f, 0.06f}, Opacity));
}

// The reference Display tab also contains renderer controls; retain only its interface-scale row.
float AppearanceInspector::ConstructDisplayTabLayout(
    PixelSpace           &Surface,
    const PlaneExtent    &Body,
    float                ScrollY,
    const ControlPointer &Pointer,
    float                Opacity) noexcept
{
    const float X       = Body.MinimumX + 16;
    const float Y       = Body.MinimumY - ScrollY;
    const float Width   = std::max(1.0f, Body.Width() - 32);
    const auto  Content = ControlKit::SectionCard(Surface, Spanning(X, Y, Width, 180), Draft.CornerRadius, Opacity);
    ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), "Interface Scaling",
                               "Editor interface only - no scene resolution controls", Ink90(), Ink50(), Opacity);
    const float RowY    = Content.MinimumY + 80;
    const auto  Control = ControlKit::ControlRow(Surface, Content.MinimumX, RowY, Content.Width(), "UI Scale",
                                                 ControlKit::Palette().TextDim, Opacity);
    char        Number[8];
    std::snprintf(Number, sizeof(Number), "%d", static_cast<int>(std::lround(Draft.InterfaceScale)));
    ControlKit::ValuePill(Surface, Control.MinimumX, RowY, Number, "%", Opacity);
    const float Left  = Control.MinimumX + ControlKit::ValuePillWidth + ControlKitTokens::RowGap;
    ScaleSliderExtent = Spanning(Left, RowY, std::max(1.0f, Control.MaximumX - Left), ControlKitTokens::ControlHeight);
    float      InterfaceScale  = Draft.InterfaceScale;
    const auto Hit = ControlKit::Slider(Surface, ScaleSliderExtent, 75, 150, InterfaceScale, DraggingSlider == 0, Pointer, InterfaceScale, Opacity);
    if (Hit.Pressed)
        DraggingSlider = 0;
    if (DraggingSlider == 0)
        Draft.InterfaceScale = std::round(InterfaceScale);
    if (Pointer.Released || !Pointer.Down)
        DraggingSlider = -1;
    return 180;
}

float AppearanceInspector::ConstructThemeTabLayout(
    PixelSpace           &Surface,
    const PlaneExtent    &Body,
    float                ScrollY,
    const ControlPointer &Pointer,
    float                Opacity) noexcept
{
    DropdownCount      = 0u;
    const float Radius = std::clamp(Draft.CornerRadius, 0.0f, SectionRadiusMax);
    const float X = Body.MinimumX, W = Body.Width();
    float       Y = Body.MinimumY - ScrollY;

    // ① Color Scheme — grid-cols-4 gap-6 gap-y-8, tiles aspect 4:3 + name (gap-3).
    {
        const float       InnerW = W - ControlKit::SectionPadding * 2.0f;
        const float       TileW = (InnerW - 3.0f * 24.0f) / 4.0f, TileH = TileW * 0.75f;
        const float       CellH   = TileH + 12.0f + 16.0f;
        const float       GridH   = CellH * 2.0f + 32.0f;
        const float       HeadH   = 20.0f + 4.0f + 16.0f + 24.0f; // title mb-1 + desc + mb-6
        const float       H       = ControlKit::SectionPadding * 2.0f + HeadH + GridH;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), "Color Scheme",
                                   "Choose the overall look of the interface", Ink90(), Ink50(), Opacity);
        for (uint32_t I = 0u; I < 8u; ++I)
        {
            const uint32_t    Col = I % 4u, Row = I / 4u;
            const PlaneExtent Tile = Spanning(Content.MinimumX + Col * (TileW + 24.0f),
                                              Content.MinimumY + HeadH + Row * (CellH + 32.0f), TileW, TileH);
            TileExtents[I]         = Tile;
            const bool Active      = Draft.Theme == TileThemes[I];
            RecordThemeTile(Surface, Tile, TileThemes[I], Active, Radius, Pointer, Opacity);
            if (Active)
                ControlKit::OutlineRounded(
                    Surface,
                    PlaneExtent{Tile.MinimumX - 2.0f, Tile.MinimumY - 2.0f, Tile.MaximumX + 2.0f, Tile.MaximumY + 2.0f},
                    ControlKit::Faded(ControlKit::Palette().Accent, Opacity), 22.0f, 2.0f);
            const PlanePoint NM = Surface.MeasureText(Tiles[I].Name, 12.0f);
            Surface.Text(Tile.MinimumX + (TileW - NM.X) * 0.5f, Tile.MaximumY + 12.0f + (16.0f - NM.Y) * 0.5f,
                         ControlKit::Faded(Active ? Ink90() : Ink50(), Opacity), Tiles[I].Name, 12.0f);
            if (ControlKit::Over(Tile, Pointer) && Pointer.Released)
                Draft.Theme = TileThemes[I];
        }
        Y += H + SectionGap;
    }

    // ② Corner Radius — heading, then the value pill + slider row below (shared sensitivity styling).
    {
        const float       HeadH   = 20.0f + 4.0f + 16.0f + 24.0f;
        const float       H       = ControlKit::SectionPadding * 2.0f + HeadH + ControlKitTokens::ControlHeight;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), "Corner Radius",
                                   "Adjust the roundness of UI elements", Ink90(), Ink50(), Opacity);
        char RadiusText[8];
        std::snprintf(RadiusText, sizeof(RadiusText), "%d", static_cast<int>(std::lround(Draft.CornerRadius)));
        ControlKit::ValuePill(Surface, Content.MinimumX, Content.MinimumY + HeadH, RadiusText, "px", Opacity);
        RadiusSliderExtent = Spanning(
            Content.MinimumX + ControlKit::ValuePillWidth + ControlKitTokens::RowGap, Content.MinimumY + HeadH,
            Content.Width() - ControlKit::ValuePillWidth - ControlKitTokens::RowGap, ControlKitTokens::ControlHeight);
        float            CornerRadius   = Draft.CornerRadius;
        const ControlHit Hit = ControlKit::Slider(Surface, RadiusSliderExtent, 0.0f, 32.0f, CornerRadius, DraggingSlider == 2,
                                                  Pointer, CornerRadius, Opacity);
        if (Hit.Pressed)
            DraggingSlider = 2;
        if (DraggingSlider == 2)
            Draft.CornerRadius = std::round(CornerRadius);
        Y += H + SectionGap;
    }

    // ③ Accent Color — flex-wrap gap-3 of 32 px swatches + dashed custom circle.
    {
        const float       HeadH   = 20.0f + 4.0f + 16.0f + 24.0f;
        const float       H       = ControlKit::SectionPadding * 2.0f + HeadH + ControlKit::SwatchDiameter + 8.0f;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), "Accent Color",
                                   "Highlight colour for active controls", Ink90(), Ink50(), Opacity);
        const float Cy = Content.MinimumY + HeadH + 4.0f + 16.0f;
        for (uint32_t I = 0u; I < 10u; ++I)
        {
            const float Cx   = Content.MinimumX + 16.0f + I * (ControlKit::SwatchDiameter + 12.0f);
            AccentExtents[I] = Spanning(Cx - 16.0f, Cy - 16.0f, 32.0f, 32.0f);
            if (ControlKit::Swatch(Surface, Cx, Cy, QueryAccentColour(static_cast<AccentCategory>(I)),
                                   static_cast<uint32_t>(Draft.Accent) == I, true, Pointer, Opacity)
                    .Clicked)
                Draft.Accent = static_cast<AccentCategory>(I);
        }
        Y += H + SectionGap;
    }

    // ④ Semantic Colors — Warning · Success · Info · Caution rows.
    {
        const float       HeadH = 20.0f + 4.0f + 16.0f + 24.0f;
        const float       RowH = 100.0f, RowGap = 32.0f + 1.0f + 32.0f; // pb-8 border-b space-y-8
        const float       H       = ControlKit::SectionPadding * 2.0f + HeadH + RowH * 4.0f + RowGap * 3.0f;
        const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        ControlKit::SectionHeading(Surface, Content.MinimumX, Content.MinimumY, Content.Width(), "Semantic Colors",
                                   "Tones used by alerts, dialogues and toasts", Ink90(), Ink50(), Opacity);
        float RowY = Content.MinimumY + HeadH;
        RecordSemanticRow(Surface, Content.MinimumX, RowY, Content.Width(), 0u, "Warning", Draft.WarningSwatch,
                          ControlCentreIconCategory::TriangleAlert, Radius, false, Pointer, Opacity);
        RowY += RowH + RowGap;
        RecordSemanticRow(Surface, Content.MinimumX, RowY, Content.Width(), 1u, "Success", Draft.SuccessSwatch,
                          ControlCentreIconCategory::CircleCheck, Radius, false, Pointer, Opacity);
        RowY += RowH + RowGap;
        RecordSemanticRow(Surface, Content.MinimumX, RowY, Content.Width(), 2u, "Info", Draft.InfoSwatch,
                          ControlCentreIconCategory::CircleInfo, Radius, false, Pointer, Opacity);
        RowY += RowH + RowGap;
        RecordSemanticRow(Surface, Content.MinimumX, RowY, Content.Width(), 3u, "Caution", Draft.CautionSwatch,
                          ControlCentreIconCategory::OctagonAlert, Radius, true, Pointer, Opacity);
        Y += H + SectionGap;
    }

    if (Pointer.Released)
        DraggingSlider = -1;
    return (Y + ScrollY) - Body.MinimumY - SectionGap;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       FONTS TAB
//------------------------------------------------------------------------------------------------------------------------
//    Notch FontsTab.tsx — Typography strip · Typeface & Colors playground · Type Scale cards · Font Rendering.

namespace
{

struct TypeRole
{
    const char *Label;
    const char *Sample;
};
constexpr TypeRole Roles[AppearanceSettings::TypeRoleCount] = {
    {"Title", "Display Title"},      {"Header", "Section Header"},
    {"Subheader", "Card Subheader"}, {"Body", "The quick brown fox jumps over the lazy dog."},
    {"Label", "Form Label"},         {"Caption", "Small caption text"},
};

// Notch WEIGHTS[] chip order is alphabetical: Bold, ExtraBold, ExtraLight, Light, Medium, Regular, SemiBold (+ Thin,
// Black appended).
constexpr FontWeightCategory ChipOrder[9] = {
    FontWeightCategory::Bold,     FontWeightCategory::ExtraBold, FontWeightCategory::ExtraLight,
    FontWeightCategory::Light,    FontWeightCategory::Medium,    FontWeightCategory::Regular,
    FontWeightCategory::SemiBold, FontWeightCategory::Thin,      FontWeightCategory::Black};

void HexOf(ColorQuad C, char *Out, size_t N)
{
    std::snprintf(Out, N, "#%02X%02X%02X", static_cast<int>(C.Red * 255.0f + 0.5f),
                  static_cast<int>(C.Green * 255.0f + 0.5f), static_cast<int>(C.Blue * 255.0f + 0.5f));
}

} // namespace

const char *AppearanceInspector::QueryTypeRoleLabel(uint32_t Role) noexcept
{
    return Roles[std::min(Role, AppearanceSettings::TypeRoleCount - 1u)].Label;
}

void *AppearanceInspector::QueryAppliedFace(uint32_t Role) const noexcept
{
    const TypefaceRegistry *Reg = TypefaceRegistry::QueryCurrent();
    if (!Reg || Role >= AppearanceSettings::TypeRoleCount)
        return nullptr;
    return Reg->QueryHandle(Applied.FontFamily, Applied.RoleWeight[Role]);
}

PlaneExtent AppearanceInspector::QueryRoleWeightChipExtent(uint32_t Role, FontWeightCategory Weight) const noexcept
{
    if (Role >= AppearanceSettings::TypeRoleCount)
        return {};
    return ChipExtents[Role][TypefaceRegistry::WeightOrdinal(Weight)];
}

void AppearanceInspector::IntegrateFontStrip(float DeltaSeconds) noexcept
{
    // scroll-smooth: exponential approach, ~200 ms
    const float Room  = std::max(StripContentWidth - StripViewWidth, 0.0f);
    StripScrollTarget = std::clamp(StripScrollTarget, 0.0f, Room);
    const float K     = 1.0f - std::exp(-DeltaSeconds * 18.0f);
    StripScroll += (StripScrollTarget - StripScroll) * K;
    if (std::fabs(StripScrollTarget - StripScroll) < 0.25f)
        StripScroll = StripScrollTarget;
}

float AppearanceInspector::ConstructFontsTabLayout(
    PixelSpace           &Surface,
    const PlaneExtent    &Body,
    float                ScrollY,
    const ControlPointer &Pointer,
    float                Opacity) noexcept
{
    DropdownCount                  = 0u;
    const TypefaceRegistry *Reg    = TypefaceRegistry::QueryCurrent();
    const float             Radius = std::clamp(Draft.CornerRadius, 0.0f, SectionRadiusMax);
    const float             X = Body.MinimumX, W = Body.Width();
    float                   Y           = Body.MinimumY - ScrollY;
    const ColorQuad         Accent      = QueryAccentColour(Draft.Accent);
    const uint32_t          FamilyCount = Reg ? Reg->QueryFamilyCount() : 0u;
    FontCardExtents.resize(FamilyCount);
    if (Reg && Draft.FontFamily >= FamilyCount && FamilyCount > 0u)
        Draft.FontFamily = 0u;
    auto Face = [&](FontWeightCategory Wt) -> void * { return Reg ? Reg->QueryHandle(Draft.FontFamily, Wt) : nullptr; };
    const char *FamilyName =
        (Reg && Reg->QueryFamily(Draft.FontFamily)) ? Reg->QueryFamily(Draft.FontFamily)->Name.c_str() : "Default";
    constexpr float SpaceY10 = 40.0f; // space-y-10

    // ① Typography — heading row + ‹ › + horizontal card strip (flex gap-4, cards w-48 p-5, pb-4 under the strip).
    {
        const float HeadH = 20.0f + 4.0f + 16.0f; // title + gap-1 + desc  (mb-4 below)
        Surface.Text(X, Y + 2.0f, ControlKit::Faded(Ink90(), Opacity), "Typography", 14.0f);
        Surface.Text(X, Y + 24.0f, ControlKit::Faded(Ink50(), Opacity), "Typeface, type scale, and font weights",
                     12.0f);
        const float BtnCy  = Y + HeadH - 16.0f; // items-end
        StripForwardExtent = Spanning(X + W - 32.0f, BtnCy - 16.0f, 32.0f, 32.0f);
        StripBackExtent    = Spanning(X + W - 32.0f - 8.0f - 32.0f, BtnCy - 16.0f, 32.0f, 32.0f);
        if (ControlKit::RoundIconButton(Surface, StripBackExtent.MinimumX + 16.0f, BtnCy,
                                        ControlCentreIconCategory::ChevronBack, Pointer, Opacity)
                .Clicked)
            StripScrollTarget -= 300.0f;
        if (ControlKit::RoundIconButton(Surface, StripForwardExtent.MinimumX + 16.0f, BtnCy,
                                        ControlCentreIconCategory::ChevronForward, Pointer, Opacity)
                .Clicked)
            StripScrollTarget += 300.0f;
        Y += HeadH + 16.0f;

        const float CardW = 192.0f, CardGap = 16.0f, CardPad = 20.0f;
        const float CardH = CardPad + 32.0f + 16.0f + 20.0f + 4.0f + 16.0f +
                            CardPad; // Aa(text-2xl ≈32) mb-4, name 20 mb-1, sentence 16
        StripViewWidth          = W;
        StripContentWidth       = FamilyCount * (CardW + CardGap) - CardGap;
        const PlaneExtent Strip = Spanning(X, Y, W, CardH);
        Surface.PushClip(PlaneExtent{Strip.MinimumX, Strip.MinimumY - 2.0f, Strip.MaximumX, Strip.MaximumY + 2.0f});
        // horizontal wheel / shift-less: the host maps vertical wheel to page scroll, so the strip uses the arrows only
        // (Notch: same).
        for (uint32_t I = 0u; I < FamilyCount; ++I)
        {
            const TypefaceFamily *Fam  = Reg->QueryFamily(I);
            const PlaneExtent     Card = Spanning(X + I * (CardW + CardGap) - StripScroll, Y, CardW, CardH);
            FontCardExtents[I]         = Card;
            const bool     Active      = Draft.FontFamily == I;
            ControlPointer Local       = Pointer;
            Local.Enabled              = Pointer.Enabled && Strip.Encloses(Pointer.X, Pointer.Y);
            const bool Hover           = ControlKit::Over(Card, Local);
            if (Active)
                Surface.FillRectangle(Card, ControlKit::Faded(ColorQuad{1.0f, 1.0f, 1.0f, 0.05f}, Opacity),
                                      Radius); // fontActiveBg
            else if (Hover)
                Surface.FillRectangle(Card, ControlKit::Faded(ColorQuad{1.0f, 1.0f, 1.0f, 0.05f}, Opacity),
                                      Radius); // hover:activeBg
            ControlKit::OutlineRounded(Surface, Card, ControlKit::Faded(Active ? Accent : Ink10(), Opacity), Radius);
            Surface.PushTypeface(Reg->QueryHandle(I, FontWeightCategory::Regular));
            Surface.Text(Card.MinimumX + CardPad, Card.MinimumY + CardPad, ControlKit::Faded(Ink90(), Opacity), "Aa",
                         24.0f);
            Surface.PopTypeface();
            Surface.PushTypeface(Face(FontWeightCategory::Medium));
            Surface.Text(Card.MinimumX + CardPad, Card.MinimumY + CardPad + 32.0f + 16.0f + 2.0f,
                         ControlKit::Faded(Ink90(), Opacity), Fam->Name.c_str(), 14.0f);
            Surface.PopTypeface();
            Surface.PushClip(
                PlaneExtent{Card.MinimumX + CardPad, Card.MinimumY, Card.MaximumX - CardPad, Card.MaximumY});
            Surface.Text(Card.MinimumX + CardPad, Card.MinimumY + CardPad + 32.0f + 16.0f + 20.0f + 4.0f + 2.0f,
                         ControlKit::Faded(Ink50(), Opacity), "The quick brown fox jumps", 12.0f);
            Surface.PopClip();
            if (Hover && Pointer.Released)
                Draft.FontFamily = I;
        }
        Surface.PopClip();
        Y += CardH + 16.0f + SpaceY10; // pb-4 + space-y-10
    }

    // ② Typeface & Colors playground — p-6 card; grid [1fr auto] gap-8: preview box (p-10) | column min-w-280.
    {
        const float HeadH   = 20.0f + 24.0f; // text-sm bold mb-6
        const float ColW    = 280.0f;
        const float ColumnH = (3.0f * 18.0f + 2.0f * 4.0f) + 32.0f + (16.0f + 4.0f + 16.0f + 16.0f) + 32.0f +
                              (48.0f + 8.0f + 4.0f + 2.0f * 13.0f + 2.0f);
        const float       PreviewH = 40.0f + 72.0f + 16.0f + 20.0f + 40.0f;
        const float       InnerH   = std::max(ColumnH, PreviewH);
        const float       H        = ControlKit::SectionPadding * 2.0f + HeadH + InnerH;
        const PlaneExtent Content  = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
        Surface.Text(Content.MinimumX, Content.MinimumY + 2.0f, ControlKit::Faded(Ink90(), Opacity),
                     "Typeface & Colors", 14.0f);

        const float       Top     = Content.MinimumY + HeadH;
        const PlaneExtent Preview = PlaneExtent{Content.MinimumX, Top, Content.MaximumX - 32.0f - ColW, Top + InnerH};
        Surface.FillRectangle(Preview, ControlKit::Faded(ColorQuad{1.0f, 1.0f, 1.0f, 0.05f}, Opacity), Radius * 0.8f);
        ControlKit::OutlineRounded(Surface, Preview, ControlKit::Faded(Ink10(), Opacity), Radius * 0.8f);
        {
            Surface.PushClip(Preview);
            const float Cy = (Preview.MinimumY + Preview.MaximumY) * 0.5f - (72.0f + 16.0f + 20.0f) * 0.5f;
            Surface.PushTypeface(Face(FontWeightCategory::Bold));
            Surface.Text(Preview.MinimumX + 40.0f, Cy, ControlKit::Faded(Ink90(), Opacity), FamilyName, 72.0f);
            Surface.PopTypeface();
            Surface.Text(Preview.MinimumX + 40.0f, Cy + 72.0f + 16.0f, ControlKit::Faded(Ink50(), Opacity),
                         "(72px bold)", 14.0f);
            Surface.PopClip();
        }
        {
            const float Cx = Content.MaximumX - ColW;
            float       Cy = Top + (InnerH - ColumnH) * 0.5f; // justify-center
            // Mono glyph rows: text-[11px] leading-relaxed, font-mono — rendered in JetBrains Mono when present.
            void *Mono = nullptr;
            if (Reg)
            {
                const int32_t M = Reg->ResolveFamilyIndex("JetBrains Mono");
                if (M >= 0)
                    Mono = Reg->QueryHandle(static_cast<uint32_t>(M), FontWeightCategory::Regular);
            }
            Surface.PushTypeface(Mono);
            Surface.Text(Cx, Cy, ControlKit::Faded(Ink50(), Opacity), "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 11.0f);
            Surface.Text(Cx, Cy + 22.0f, ControlKit::Faded(Ink50(), Opacity), "abcdefghijklmnopqrstuvwxyz", 11.0f);
            Surface.Text(Cx, Cy + 44.0f, ControlKit::Faded(Ink50(), Opacity), "0123456789 !@#$%^&*()", 11.0f);
            Surface.PopTypeface();
            Cy += 3.0f * 18.0f + 2.0f * 4.0f + 32.0f;
            char Hex[8];
            HexOf(Accent, Hex, sizeof(Hex));
            char Line[96];
            Surface.PushTypeface(Face(FontWeightCategory::SemiBold));
            Surface.Text(Cx, Cy, ControlKit::Faded(Accent, Opacity), "Accent", 12.0f);
            const float AccW = Surface.MeasureText("Accent", 12.0f).X;
            Surface.PopTypeface();
            Surface.PushTypeface(Face(FontWeightCategory::Regular));
            Surface.Text(Cx + AccW, Cy, ControlKit::Faded(Accent, Opacity),
                         " \xE2\x80\x94 The quick brown fox jumps over...", 12.0f);
            std::snprintf(Line, sizeof(Line), "(rendered in %s)", Hex);
            ColorQuad AccentDim = Accent;
            AccentDim.Alpha *= 0.7f;
            Surface.Text(Cx, Cy + 20.0f, ControlKit::Faded(AccentDim, Opacity), Line, 12.0f);
            Surface.PopTypeface();
            Cy += 16.0f + 4.0f + 16.0f + 16.0f + 32.0f;
            // Three discs: accent / #FFFFFF / #000000 with hex + label (gap-6, 48 px, mt-1, 10 px lines)
            struct Disc
            {
                ColorQuad   Colour;
                const char *Label;
            } Discs[3] = {{Accent, "Accent"},
                          {ColorQuad{1.0f, 1.0f, 1.0f, 1.0f}, "Primary"},
                          {ColorQuad{0.0f, 0.0f, 0.0f, 1.0f}, "Background"}};
            float Dx   = Cx;
            for (const Disc &D : Discs)
            {
                ControlKit::FillCircle(Surface, Dx + 24.0f, Cy + 24.0f, 24.0f, ControlKit::Faded(D.Colour, Opacity));
                ControlKit::OutlineCircle(Surface, Dx + 24.0f, Cy + 24.0f, 23.5f,
                                          ControlKit::Faded(ColorQuad{1.0f, 1.0f, 1.0f, 0.05f}, Opacity), 1.0f);
                HexOf(D.Colour, Hex, sizeof(Hex));
                Surface.PushTypeface(Face(FontWeightCategory::SemiBold));
                Surface.Text(Dx, Cy + 48.0f + 12.0f, ControlKit::Faded(Ink90(), Opacity), Hex, 10.0f);
                Surface.PopTypeface();
                Surface.Text(Dx, Cy + 48.0f + 12.0f + 15.0f, ControlKit::Faded(Ink50(), Opacity), D.Label, 10.0f);
                Dx += std::max(48.0f, Surface.MeasureText(D.Label, 10.0f).X) + 24.0f;
            }
        }
        Y += H + SpaceY10;
    }

    // ③ Type Scale — Desktop: uppercase tracked heading, then per-role cards grid [300px 1fr] gap-8.
    {
        Surface.Text(X, Y + 2.0f, ControlKit::Faded(Ink50(), Opacity), "TYPE SCALE - DESKTOP", 12.0f);
        Y += 16.0f + 24.0f + 16.0f; // mb-6 + pt-4
        for (uint32_t R = 0u; R < AppearanceSettings::TypeRoleCount; ++R)
        {
            // Chips for the weights this family actually ships (Notch: fixed seven; ours: per-family, flagged).
            uint32_t           ChipCount = 0u;
            FontWeightCategory Chips[9];
            float              ChipW[9];
            for (FontWeightCategory Wt : ChipOrder)
                if (!Reg || Reg->ContainsWeight(Draft.FontFamily, Wt))
                {
                    Chips[ChipCount] = Wt;
                    ChipW[ChipCount] = ControlKit::ChipWidth(Surface, TypefaceRegistry::WeightLabel(Wt));
                    ++ChipCount;
                }
            // Snap the role's weight to a shipped face so the chip row always shows a selection.
            if (Reg)
            {
                const TypefaceFace *Snap = Reg->Resolve(Draft.FontFamily, Draft.RoleWeight[R]);
                if (Snap)
                    Draft.RoleWeight[R] = Snap->Weight;
            }

            // Wrap chips into rows of 300 px (flex-wrap gap-3).
            const float LeftW      = 300.0f;
            uint32_t    RowsNeeded = 1u;
            {
                float Cx = 0.0f;
                for (uint32_t C = 0u; C < ChipCount; ++C)
                {
                    if (Cx > 0.0f && Cx + ChipW[C] > LeftW)
                    {
                        ++RowsNeeded;
                        Cx = 0.0f;
                    }
                    Cx += ChipW[C] + 12.0f;
                }
            }
            const float LeftH = 20.0f + 24.0f + ControlKit::SliderHeight + 32.0f +
                                RowsNeeded * ControlKit::ChipHeight + (RowsNeeded - 1u) * 12.0f;
            const float       H       = ControlKit::SectionPadding * 2.0f + std::max(LeftH, 100.0f);
            const PlaneExtent Content = ControlKit::SectionCard(Surface, Spanning(X, Y, W, H), Radius, Opacity);
            const float       Top     = Content.MinimumY + (Content.Height() - LeftH) * 0.5f; // items-center

            // Left: label / value pill + slider / chips
            char Px[16];
            std::snprintf(Px, sizeof(Px), "%d", static_cast<int>(std::lround(Draft.RoleSize[R])));
            Surface.PushTypeface(Face(FontWeightCategory::Medium));
            Surface.Text(Content.MinimumX, Top + 2.0f, ControlKit::Faded(Ink90(), Opacity), Roles[R].Label, 14.0f);
            Surface.PopTypeface();
            const float SliderY =
                Top + 20.0f + 24.0f - (ControlKitTokens::ControlHeight - ControlKit::SliderHeight) * 0.5f;
            ControlKit::ValuePill(Surface, Content.MinimumX, SliderY, Px, "px", Opacity);
            RoleSliderExtents[R] = Spanning(Content.MinimumX + ControlKit::ValuePillWidth + ControlKitTokens::RowGap,
                                            SliderY, LeftW - ControlKit::ValuePillWidth - ControlKitTokens::RowGap,
                                            ControlKitTokens::ControlHeight);
            {
                float            TypeSize       = Draft.RoleSize[R];
                const int        Ordinal = 10 + static_cast<int>(R);
                const ControlHit Hit     = ControlKit::Slider(Surface, RoleSliderExtents[R], 8.0f, 72.0f, TypeSize,
                                                              DraggingSlider == Ordinal, Pointer, TypeSize, Opacity);
                if (Hit.Pressed)
                    DraggingSlider = Ordinal;
                if (DraggingSlider == Ordinal)
                    Draft.RoleSize[R] = std::round(TypeSize);
            }
            {
                float Cx = Content.MinimumX, Cy = Top + 20.0f + 24.0f + ControlKit::SliderHeight + 32.0f;
                for (uint32_t O = 0u; O < 9u; ++O)
                    ChipExtents[R][O] = PlaneExtent{};
                for (uint32_t C = 0u; C < ChipCount; ++C)
                {
                    if (Cx > Content.MinimumX && Cx - Content.MinimumX + ChipW[C] > LeftW)
                    {
                        Cx = Content.MinimumX;
                        Cy += ControlKit::ChipHeight + 12.0f;
                    }
                    const PlaneExtent Chip = Spanning(Cx, Cy, ChipW[C], ControlKit::ChipHeight);
                    ChipExtents[R][TypefaceRegistry::WeightOrdinal(Chips[C])] = Chip;
                    if (ControlKit::ChipButton(Surface, Chip, TypefaceRegistry::WeightLabel(Chips[C]),
                                               Draft.RoleWeight[R] == Chips[C], Pointer, Opacity)
                            .Clicked)
                        Draft.RoleWeight[R] = Chips[C];
                    Cx += ChipW[C] + 12.0f;
                }
            }

            // Right: preview box p-6, radius × 0.7, min-h 100, sample text truncated at the role's size/weight.
            const PlaneExtent Box =
                PlaneExtent{Content.MinimumX + LeftW + 32.0f, Content.MinimumY, Content.MaximumX, Content.MaximumY};
            Surface.FillRectangle(Box, ControlKit::Faded(ColorQuad{1.0f, 1.0f, 1.0f, 0.05f}, Opacity), Radius * 0.7f);
            ControlKit::OutlineRounded(Surface, Box, ControlKit::Faded(Ink10(), Opacity), Radius * 0.7f);
            Surface.PushClip(PlaneExtent{Box.MinimumX + 24.0f, Box.MinimumY, Box.MaximumX - 24.0f, Box.MaximumY});
            Surface.PushTypeface(Face(Draft.RoleWeight[R]));
            const float LineH = Surface.MeasureText("Ag", Draft.RoleSize[R]).Y;
            Surface.Text(Box.MinimumX + 24.0f, (Box.MinimumY + Box.MaximumY) * 0.5f - LineH * 0.5f,
                         ControlKit::Faded(Ink90(), Opacity), Roles[R].Sample, Draft.RoleSize[R]);
            Surface.PopTypeface();
            Surface.PopClip();

            Y += H + 32.0f; // space-y-8
        }
        Y += SpaceY10 - 32.0f;
    }

    if (Pointer.Released)
        DraggingSlider = -1;
    return (Y + ScrollY) - Body.MinimumY - SectionGap;
}

} // namespace Frontier
