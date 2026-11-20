/*============================================================================================================================================
                                                             THEMEPREVIEWTILE.CPP
============================================================================================================================================*/
// 🧩 Exact 1:1 miniature window theme preview tiles and circular accent/semantic swatches.

#include "Engine/Components/Controls/ThemePreviewTile.h"

#include <algorithm>

namespace Frontier
{

namespace
{

struct MiniatureThemeScheme
{
    const char *Label;
    ThemePreset Preset;
    ColorQuad   Background;
    ColorQuad   Sidebar;
    ColorQuad   Pane;
    ColorQuad   Lines;
    bool        OutlinePane;
};

constexpr MiniatureThemeScheme DisplaySchemes[8] = {
    {"OLED", ThemePreset::Oled, ColorQuad::FromHex(0x000000u), ColorQuad::FromHex(0x0A0A0Au), ColorQuad::FromHex(0x141414u), ColorQuad::FromHex(0xFFFFFFu), false},
    {"Dark", ThemePreset::Dark, ColorQuad::FromHex(0x18181Au), ColorQuad::FromHex(0x222224u), ColorQuad::FromHex(0x2C2C2Eu), ColorQuad::FromHex(0xFFFFFFu), false},
    {"Light", ThemePreset::Light, ColorQuad::FromHex(0xE5E5E5u), ColorQuad::FromHex(0xF4F4F5u), ColorQuad::FromHex(0xFFFFFFu), ColorQuad::FromHex(0x000000u), true},
    {"Sepia", ThemePreset::Sepia, ColorQuad::FromHex(0xDCA85Bu), ColorQuad::FromHex(0xE6C697u), ColorQuad::FromHex(0xF1DAB0u), ColorQuad::FromHex(0x825619u), false},
    {"Dracula", ThemePreset::Dracula, ColorQuad::FromHex(0x282A36u), ColorQuad::FromHex(0x44475Au), ColorQuad::FromHex(0x6272A4u), ColorQuad::FromHex(0xF8F8F2u), false},
    {"Nord", ThemePreset::Nord, ColorQuad::FromHex(0x2E3440u), ColorQuad::FromHex(0x3B4252u), ColorQuad::FromHex(0x4C566Au), ColorQuad::FromHex(0xECEFF4u), false},
    {"GitHub", ThemePreset::GitHub, ColorQuad::FromHex(0x0D1117u), ColorQuad::FromHex(0x161B22u), ColorQuad::FromHex(0x21262Du), ColorQuad::FromHex(0xC9D1D9u), false},
    {"Dim", ThemePreset::Dim, ColorQuad::FromHex(0x0F172Au), ColorQuad::FromHex(0x1E293Bu), ColorQuad::FromHex(0x334155u), ColorQuad::FromHex(0xE2E8F0u), false},
};

} // namespace

namespace ThemePreviewTile
{

ThemePreset PresetAtDisplayOrder(uint32_t DisplayOrdinal) noexcept
{
    return DisplaySchemes[std::min(DisplayOrdinal, 7u)].Preset;
}

const char *LabelAtDisplayOrder(uint32_t DisplayOrdinal) noexcept
{
    return DisplaySchemes[std::min(DisplayOrdinal, 7u)].Label;
}

ControlContact Construct(PixelSpace &Surface, Rectangle2D TileBounds, ThemePreset Preset, bool Active, float CornerRadius,
                    const ControlPointer &Pointer, float Opacity) noexcept
{
    const ControlColorSet &Colors = ControlAppearance::Colors();
    ControlContact         Contact{};
    Contact.Hovered = ControlAppearance::QueryPointerOver(TileBounds, Pointer);
    Contact.Pressed = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked = Contact.Hovered && Pointer.Released;

    uint32_t SchemeIndex = 0u;
    for (uint32_t Index = 0u; Index < 8u; ++Index)
    {
        if (DisplaySchemes[Index].Preset == Preset)
            SchemeIndex = Index;
    }
    const MiniatureThemeScheme &Scheme = DisplaySchemes[SchemeIndex];

    const ColorQuad IdleRing = Colors.LightSurface ? ColorQuad{0.0f, 0.0f, 0.0f, Contact.Hovered ? 0.20f : 0.10f}
                                                    : ColorQuad{1.0f, 1.0f, 1.0f, Contact.Hovered ? 0.20f : 0.10f};

    Surface.FillRectangle(TileBounds, Scheme.Background.Faded(Opacity).ToRgba8(), 20.0f);
    if (!Active)
        ControlAppearance::ConstructRoundedOutline(Surface, TileBounds, IdleRing.Faded(Opacity), 20.0f);

    Surface.PushClip(TileBounds);
    const Rectangle2D Inner{TileBounds.X + 24.0f, TileBounds.Y + 24.0f, std::max(0.0f, TileBounds.Width - 24.0f),
                            std::max(0.0f, TileBounds.Height - 24.0f)};
    Surface.FillRectangle(Inner, Scheme.Sidebar.Faded(Opacity).ToRgba8(), 0.0f);

    const float       SideW = Inner.Width * 0.30f;
    const Rectangle2D Side{Inner.X, Inner.Y, SideW, Inner.Height};
    const Rectangle2D Pane{Side.Right(), Inner.Y, Inner.Width - SideW, Inner.Height};
    Surface.FillRectangle(Pane, Scheme.Pane.Faded(Opacity).ToRgba8(), 0.0f);
    if (Scheme.OutlinePane)
        ControlAppearance::ConstructRoundedOutline(Surface, Pane, ColorQuad{0.0f, 0.0f, 0.0f, 0.05f * Opacity}, 0.0f);
    Surface.FillRectangle({Side.Right(), Side.Y, 1.0f, Side.Height},
                          ColorQuad{0.0f, 0.0f, 0.0f, 0.05f * Opacity}.ToRgba8(), 0.0f);

    const float LineRadius = CornerRadius * 0.25f;
    auto        ConstructMockBar = [&](float BarX, float BarY, float BarW, float BarH, float Alpha, float Rad) {
        Surface.FillRectangle({BarX, BarY, BarW, BarH},
                              ColorQuad{Scheme.Lines.Red, Scheme.Lines.Green, Scheme.Lines.Blue, Alpha * Opacity}.ToRgba8(),
                              Rad);
    };

    float       SideX = Side.X + 12.0f;
    float       SideY = Side.Y + 12.0f;
    const float SideInnerW = std::max(0.0f, Side.Width - 24.0f);
    for (int Dot = 0; Dot < 3; ++Dot)
        ConstructMockBar(SideX + static_cast<float>(Dot) * 10.0f, SideY, 6.0f, 6.0f, 0.4f, 3.0f);
    SideY += 14.0f;
    ConstructMockBar(SideX, SideY, SideInnerW, 6.0f, 0.2f, LineRadius);
    SideY += 12.0f;
    ConstructMockBar(SideX, SideY, SideInnerW * (2.0f / 3.0f), 6.0f, 0.2f, LineRadius);
    SideY += 12.0f;
    ConstructMockBar(SideX, SideY, SideInnerW * 0.5f, 6.0f, 0.2f, LineRadius);

    const float AvatarY = Side.Bottom() - 22.0f;
    if (AvatarY > SideY + 8.0f)
    {
        ConstructMockBar(SideX, AvatarY, 10.0f, 10.0f, 0.4f, 5.0f);
        ConstructMockBar(SideX + 16.0f, AvatarY + 2.0f, 16.0f, 6.0f, 0.2f, LineRadius);
    }

    const float PaneX      = Pane.X + 16.0f;
    float       PaneY      = Pane.Y + 16.0f;
    const float PaneInnerW = std::max(0.0f, Pane.Width - 32.0f);
    ConstructMockBar(PaneX, PaneY, PaneInnerW * 0.25f, 6.0f, 0.3f, LineRadius);
    PaneY += 16.0f;
    ConstructMockBar(PaneX, PaneY, PaneInnerW / 3.0f, 6.0f, 0.2f, LineRadius);
    PaneY += 16.0f;
    const float SquareSide = std::max((PaneInnerW - 16.0f) / 3.0f, 4.0f);
    for (int Box = 0; Box < 3; ++Box)
        ConstructMockBar(PaneX + static_cast<float>(Box) * (SquareSide + 8.0f), PaneY, SquareSide, SquareSide, 0.1f,
                    CornerRadius * 0.5f);
    const float BottomLineY = Pane.Bottom() - 22.0f;
    if (BottomLineY > PaneY + SquareSide + 4.0f)
        ConstructMockBar(PaneX, BottomLineY, PaneInnerW * 0.2f, 6.0f, 0.2f, LineRadius);
    Surface.PopClip();

    if (Active)
    {
        ControlAppearance::ConstructRoundedOutline(
            Surface, {TileBounds.X - 2.0f, TileBounds.Y - 2.0f, TileBounds.Width + 4.0f, TileBounds.Height + 4.0f},
            Colors.Accent.Faded(Opacity), 22.0f, 2.0f);
    }

    const Point2D NameExtent = Surface.MeasureText(12.0f, Scheme.Label);
    Surface.Text({TileBounds.X + (TileBounds.Width - NameExtent.X) * 0.5f,
                  TileBounds.Bottom() + 12.0f + (16.0f - NameExtent.Y) * 0.5f},
                 12.0f, (Active ? Colors.Text : Colors.TextDim).Faded(Opacity).ToRgba8(), Scheme.Label);
    return Contact;
}

ControlContact ConstructColorSwatch(PixelSpace &Surface, float CenterX, float CenterY, ColorQuad SwatchColor, bool Selected,
                               bool OuterRingStyle, const ControlPointer &Pointer, float Opacity) noexcept
{
    const ControlColorSet &Colors = ControlAppearance::Colors();
    const float            Radius  = ControlMetrics::SwatchDiameter * 0.5f;
    const Rectangle2D      Bounds{CenterX - Radius, CenterY - Radius, ControlMetrics::SwatchDiameter,
                             ControlMetrics::SwatchDiameter};

    ControlContact Contact{};
    Contact.Hovered         = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    Contact.Pressed         = Contact.Hovered && Pointer.Pressed;
    Contact.Clicked         = Contact.Hovered && Pointer.Released;
    const float HoverScale  = (Contact.Hovered && OuterRingStyle) ? 1.10f : 1.0f;

    ControlAppearance::ConstructCircleFill(Surface, CenterX, CenterY, Radius * HoverScale, SwatchColor.Faded(Opacity));
    if (Selected)
    {
        if (OuterRingStyle)
        {
            ControlAppearance::ConstructCircleOutline(Surface, CenterX, CenterY, Radius + 4.0f, SwatchColor.Faded(Opacity), 2.0f);
            ControlAppearance::ConstructCircleFill(Surface, CenterX, CenterY, 4.0f, Colors.Field.Faded(Opacity));
        }
        else
        {
            ControlAppearance::ConstructCircleOutline(Surface, CenterX, CenterY, Radius - 1.0f, Colors.Primary.Faded(Opacity),
                                            2.0f);
        }
    }
    return Contact;
}

} // namespace ThemePreviewTile

} // namespace Frontier
