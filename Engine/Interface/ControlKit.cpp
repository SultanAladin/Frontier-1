/*==========================================================================================================================================
                                                               CONTROLKIT.CPP
==========================================================================================================================================*/
// 🧩 Render shared themed controls, surface colours, and pointer interaction.

#include "ControlKit.h"
#include "GlyphSpace.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier
{

namespace
{

constexpr float Pi = 3.14159265358979f;

float PillRadius(const PlaneExtent &Extent) noexcept
{
    return std::min(Extent.Width(), Extent.Height()) * 0.5f;
}

void FillCircle(PixelSpace &Surface, float Cx, float Cy, float R, ColorQuad Colour) noexcept
{
    Surface.FillRectangle(Spanning(Cx - R, Cy - R, R * 2.0f, R * 2.0f), Colour, R);
}

} // namespace

ControlKitPalette ControlKit::ActivePalette{};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THEME
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Notch: hover:bg-white/5 on dark themes, hover:bg-black/5 on light ones (OutlinerPanel: 'hover:bg-black/5 dark:hover:bg-white/5').
ColorQuad HoverWash() noexcept
{
    return ControlKit::Palette().LightSurface ? ColorQuad{0.0f, 0.0f, 0.0f, 0.05f} : ColorQuad{1.0f, 1.0f, 1.0f, 0.05f};
}

float Luminance(ColorQuad C) noexcept
{
    return 0.2126f * C.Red + 0.7152f * C.Green + 0.0722f * C.Blue;
}

// Linear blend between two theme colours, for the slots the theme does not name directly.
ColorQuad Blend(ColorQuad From, ColorQuad To, float T) noexcept
{
    return ColorQuad{From.Red + (To.Red - From.Red) * T, From.Green + (To.Green - From.Green) * T,
                     From.Blue + (To.Blue - From.Blue) * T, From.Alpha + (To.Alpha - From.Alpha) * T};
}

} // namespace

void ControlKit::ConfigureTheme(const ThemeStructure &Theme, ColorQuad WarningColour, ColorQuad SuccessColour,
                                ColorQuad InfoColour, ColorQuad CautionColour) noexcept
{
    const ThemePalette &P = Theme.QueryPalette();
    ControlKitPalette  &K = ActivePalette;
    K                     = ControlKitPalette{}; // geometry-free defaults, then override every themed slot
    K.LightSurface        = Luminance(P.MainBackground) > 0.5f;
    K.TabBackground       = Theme.QueryDarkestSurface();
    K.Panel               = P.PanelBackground;
    K.Inset               = P.InputBackground;
    // 🔴 NOT MainBackground. The mock keeps --bg (#050505) and --field (#000000) as separate tokens, and a
    //    value pill's number cell is --field: the darkest thing on the panel, so it reads as a well cut into the
    //    card. Deriving it from the canvas made it #0e0e0e under the shipped theme — one step from the card
    //    behind it — and the pill stopped looking like a control at all. The field is the floor of the palette:
    //    black on a dark surface, and the canvas only on a light one where black would be the wrong extreme.
    K.Field        = K.LightSurface ? P.MainBackground : ColorQuad{0.0f, 0.0f, 0.0f, 1.0f};
    K.Raised       = P.ActiveBackground;
    K.Selected     = P.CardSubBackground;
    K.Stroke       = P.PanelBorder;
    K.StrokeStrong = P.CardSubBackground;
    K.Divider      = P.DividerColor;
    K.Card         = P.CardBackground;
    K.CardSub      = P.CardSubBackground;
    K.Text         = P.TextMain;
    K.TextDim      = P.TextMuted;
    K.TextFaint    = P.TextMuted;
    K.TextFaint.Alpha *= 0.7f;
    // Notch hard-codes the primary pill / active chip / segmented cell as bg-white text-black on dark themes; on light
    //    surfaces that would vanish, so the primary inverts to the text colour with the canvas as ink.
    K.Primary    = K.LightSurface ? P.TextMain : ColorQuad{1.0f, 1.0f, 1.0f, 1.0f};
    K.PrimaryInk = K.LightSurface ? P.MainBackground : ColorQuad{0x11 / 255.0f, 0x11 / 255.0f, 0x11 / 255.0f, 1.0f};
    K.Accent     = Theme.QueryAccentColor();
    K.AccentInk  = Luminance(K.Accent) > 0.8f ? ColorQuad{0x11 / 255.0f, 0x11 / 255.0f, 0x11 / 255.0f, 1.0f}
                                              : ColorQuad{1.0f, 1.0f, 1.0f, 1.0f};
    K.AccentSoft = K.Accent;
    K.AccentSoft.Alpha = 0.12f;
    K.Highlight        = K.Accent; // dashboard pill fill follows the accent

    // 🔴 The slider is NOT an accent bar. Both of these used to be the accent, which made every slider a solid
    //    blue pill with a blue thumb on top of it — the thumb invisible against its own fill, and the control
    //    reading as a progress bar rather than as something to grab. The rule is: the filled side stays dark
    //    and close to the track, "so the thumb is what carries the eye". A slider says where a value SITS; a
    //    bar says how full something is, and they should not look alike.
    //
    //    Derived from the theme rather than hard-coded, so both still follow it: the fill is the raised surface
    //    lifted a little toward the text colour, which lands on the mock's #4a4a4a against a #222 track on the
    //    dark theme and stays legible on a light one; the thumb is the text colour, which is the brightest thing
    //    the theme owns and therefore the thing the eye goes to.
    //    The three tones come from the reference project's own tokens — track #2f2f33, fill #8a8a8e, knob
    //    #f4f4f5 — reached by blending theme colours rather than written as literals, so a light theme still
    //    gets a dark knob on a light rail instead of an invisible one.
    K.SliderTrack   = Blend(P.InputBackground, P.ActiveBackground, 0.55f);
    K.SliderFill    = Blend(P.ActiveBackground, P.TextMain, 0.55f);
    K.SliderThumb   = Blend(P.TextMain, K.LightSurface ? P.MainBackground : ColorQuad{1.0f, 1.0f, 1.0f, 1.0f}, 0.35f);
    K.SwitchKnobOff = K.LightSurface ? P.TextMuted : ColorQuad{0xBD / 255.0f, 0xBD / 255.0f, 0xBD / 255.0f, 1.0f};
    K.Warning       = WarningColour;
    K.Ok            = SuccessColour;
    K.Info          = InfoColour;
    K.Caution       = CautionColour;
}

void ControlKit::BlendPalette(const ControlKitPalette &From, const ControlKitPalette &To, float T) noexcept
{
    // Live theme preview: smoothstepped cross-fade so a tile tap reads as a morph, not a snap.
    const float S   = std::clamp(T, 0.0f, 1.0f);
    const float E   = S * S * (3.0f - 2.0f * S);
    const auto  Mix = [E](const ColorQuad &A, const ColorQuad &B) noexcept -> ColorQuad
    {
        return ColorQuad{A.Red + (B.Red - A.Red) * E, A.Green + (B.Green - A.Green) * E, A.Blue + (B.Blue - A.Blue) * E,
                         A.Alpha + (B.Alpha - A.Alpha) * E};
    };
    ControlKitPalette &K = ActivePalette;
    K.TabBackground      = Mix(From.TabBackground, To.TabBackground);
    K.Panel              = Mix(From.Panel, To.Panel);
    K.Inset              = Mix(From.Inset, To.Inset);
    K.Field              = Mix(From.Field, To.Field);
    K.Raised             = Mix(From.Raised, To.Raised);
    K.Selected           = Mix(From.Selected, To.Selected);
    K.Stroke             = Mix(From.Stroke, To.Stroke);
    K.StrokeStrong       = Mix(From.StrokeStrong, To.StrokeStrong);
    K.Divider            = Mix(From.Divider, To.Divider);
    K.Card               = Mix(From.Card, To.Card);
    K.CardSub            = Mix(From.CardSub, To.CardSub);
    K.Text               = Mix(From.Text, To.Text);
    K.TextDim            = Mix(From.TextDim, To.TextDim);
    K.TextFaint          = Mix(From.TextFaint, To.TextFaint);
    K.Primary            = Mix(From.Primary, To.Primary);
    K.PrimaryInk         = Mix(From.PrimaryInk, To.PrimaryInk);
    K.Accent             = Mix(From.Accent, To.Accent);
    K.AccentInk          = Mix(From.AccentInk, To.AccentInk);
    K.AccentSoft         = Mix(From.AccentSoft, To.AccentSoft);
    K.Highlight          = Mix(From.Highlight, To.Highlight);
    K.Danger             = Mix(From.Danger, To.Danger);
    K.Ok                 = Mix(From.Ok, To.Ok);
    K.Info               = Mix(From.Info, To.Info);
    K.Warning            = Mix(From.Warning, To.Warning);
    K.Caution            = Mix(From.Caution, To.Caution);
    K.SliderTrack        = Mix(From.SliderTrack, To.SliderTrack);
    K.SliderFill         = Mix(From.SliderFill, To.SliderFill);
    K.SliderThumb        = Mix(From.SliderThumb, To.SliderThumb);
    K.SwitchKnobOff      = Mix(From.SwitchKnobOff, To.SwitchKnobOff);
    K.LightSurface       = E < 0.5f ? From.LightSurface : To.LightSurface;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

void ControlKit::OutlineRounded(PixelSpace &Surface, const PlaneExtent &Extent, ColorQuad Colour, float Radius,
                                float Thickness) noexcept
{
    std::vector<PlanePoint> Points;
    const float             R        = std::min(Radius, PillRadius(Extent));
    constexpr int           Segments = 10;
    auto                    Arc      = [&](float Cx, float Cy, float From)
    {
        for (int I = 0; I <= Segments; ++I)
        {
            const float A = From + (Pi * 0.5f) * (static_cast<float>(I) / Segments);
            Points.push_back(PlanePoint{Cx + std::cos(A) * R, Cy + std::sin(A) * R});
        }
    };
    Arc(Extent.MaximumX - R, Extent.MinimumY + R, -Pi * 0.5f);
    Arc(Extent.MaximumX - R, Extent.MaximumY - R, 0.0f);
    Arc(Extent.MinimumX + R, Extent.MaximumY - R, Pi * 0.5f);
    Arc(Extent.MinimumX + R, Extent.MinimumY + R, Pi);
    Surface.StrokePolyline(Points.data(), static_cast<uint32_t>(Points.size()), Colour, Thickness, true);
}

void ControlKit::OutlineCircle(PixelSpace &Surface, float Cx, float Cy, float R, ColorQuad Colour,
                               float Thickness) noexcept
{
    std::vector<PlanePoint> Points;
    constexpr int           Segments = 40;
    for (int I = 0; I < Segments; ++I)
    {
        const float A = (2.0f * Pi) * (static_cast<float>(I) / Segments);
        Points.push_back(PlanePoint{Cx + std::cos(A) * R, Cy + std::sin(A) * R});
    }
    Surface.StrokePolyline(Points.data(), static_cast<uint32_t>(Points.size()), Colour, Thickness, true);
}

void ControlKit::Divider(PixelSpace &Surface, float X, float Y, float Width, ColorQuad Colour) noexcept
{
    Surface.FillRectangle(Spanning(X, Y, Width, 1.0f), Colour);
}

void ControlKit::TextCentred(PixelSpace &Surface, const PlaneExtent &Extent, ColorQuad Colour, const char *Utf8,
                             float Size) noexcept
{
    const PlanePoint M = Surface.MeasureText(Utf8, Size);
    Surface.Text(Extent.MinimumX + (Extent.Width() - M.X) * 0.5f, Extent.MinimumY + (Extent.Height() - M.Y) * 0.5f,
                 Colour, Utf8, Size);
}

void ControlKit::TextLeading(PixelSpace &Surface, const PlaneExtent &Extent, float PadX, ColorQuad Colour,
                             const char *Utf8, float Size) noexcept
{
    const PlanePoint M = Surface.MeasureText(Utf8, Size);
    Surface.Text(Extent.MinimumX + PadX, Extent.MinimumY + (Extent.Height() - M.Y) * 0.5f, Colour, Utf8, Size);
}

void ControlKit::Glyph(PixelSpace &Surface, float X, float Y, float Size, ColorQuad Colour,
                       ControlCentreIconCategory Icon, float Stroke) noexcept
{
    GlyphPlacement P{};
    P.X           = X;
    P.Y           = Y;
    P.Size        = Size;
    P.StrokeWidth = Stroke;
    P.Colour      = Colour;
    GlyphSpace::Stroke(Surface, VectorCodec::QueryControlCentreSvgPath(Icon), P);
}

void ControlKit::GlyphCentred(PixelSpace &Surface, const PlaneExtent &Extent, float Size, ColorQuad Colour,
                              ControlCentreIconCategory Icon, float Stroke) noexcept
{
    Glyph(Surface, Extent.MinimumX + (Extent.Width() - Size) * 0.5f, Extent.MinimumY + (Extent.Height() - Size) * 0.5f,
          Size, Colour, Icon, Stroke);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       BUTTONS
//------------------------------------------------------------------------------------------------------------------------

float ControlKit::ButtonWidth(PixelSpace &Surface, const ButtonStructure &Button) noexcept
{
    return Surface.MeasureText(Button.Label, Button.FontSize).X + Button.PaddingX * 2.0f;
}

ControlHit ControlKit::PillButton(PixelSpace &Surface, const PlaneExtent &Extent, const ButtonStructure &Button,
                                  const ControlPointer &Pointer, float Opacity) noexcept
{
    ControlHit  Hit{};
    const float A = Opacity * (Button.Disabled ? ControlKitTokens::DisabledAlpha : 1.0f);
    Hit.Hovered   = !Button.Disabled && Over(Extent, Pointer);
    Hit.Pressed   = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked   = Hit.Hovered && Pointer.Released;

    const float R    = PillRadius(Extent);
    ColorQuad   Fill = Palette().Inset, Ink = Palette().Text, Border = Palette().Stroke;
    bool        DrawBorder = true;
    switch (Button.Tone)
    {
    case ButtonToneCategory::Primary:
        Fill = Palette().Primary;
        if (Hit.Hovered)
            Fill.Alpha *= 0.9f; // hover:bg-white/90
        Ink        = Palette().PrimaryInk;
        DrawBorder = false;
        break;
    case ButtonToneCategory::Secondary:
        Fill = Hit.Hovered ? Palette().Raised : Palette().Inset;
        break;
    case ButtonToneCategory::Ghost:
        Fill       = Hit.Hovered ? Palette().Inset : ColorQuad{0.0f, 0.0f, 0.0f, 0.0f};
        Ink        = Hit.Hovered ? Palette().Text : Palette().TextDim;
        DrawBorder = false;
        break;
    case ButtonToneCategory::Danger:
        Fill = Hit.Hovered ? Palette().Raised : Palette().Inset;
        Ink  = Palette().Danger;
        break;
    case ButtonToneCategory::Tinted:
        Fill = Button.Tint.Alpha > 0.0f ? Button.Tint : Palette().Accent;
        if (Hit.Hovered)
        {
            Fill.Red *= 0.9f;
            Fill.Green *= 0.9f;
            Fill.Blue *= 0.9f;
        }
        Ink        = ColorQuad{1.0f, 1.0f, 1.0f, 1.0f};
        DrawBorder = false;
        break;
    }
    if (Fill.Alpha > 0.0f)
        Surface.FillRectangle(Extent, Faded(Fill, A), R);
    if (DrawBorder)
        OutlineRounded(Surface, Extent, Faded(Border, A), R);
    TextCentred(Surface, Extent, Faded(Ink, A), Button.Label, Button.FontSize);
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SWITCH
//------------------------------------------------------------------------------------------------------------------------

ControlHit ControlKit::Switch(PixelSpace &Surface, float X, float Y, bool On, const ControlPointer &Pointer,
                              float Opacity) noexcept
{
    const PlaneExtent Extent = Spanning(X, Y, SwitchWidth, SwitchHeight);
    ControlHit        Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;

    Surface.FillRectangle(Extent, Faded(On ? Palette().Accent : Palette().Raised, Opacity),
                          SwitchHeight * 0.5f); // Notch FormToggle: accentColor when on
    if (!On)
        OutlineRounded(Surface, Extent, Faded(Palette().Stroke, Opacity), SwitchHeight * 0.5f);
    const float KnobX = X + 2.0f + 1.0f + (On ? 20.0f : 0.0f); // top:2px left:2px (+1 border), translateX(20px)
    FillCircle(Surface, KnobX + 10.0f, Y + 3.0f + 10.0f, 10.0f,
               Faded(On ? Palette().AccentInk : Palette().SwitchKnobOff, Opacity)); // knob bg-white on the accent
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CHIP & ROUND ICON
//------------------------------------------------------------------------------------------------------------------------

float ControlKit::ChipWidth(PixelSpace &Surface, const char *Label) noexcept
{
    return Surface.MeasureText(Label, 12.0f).X + 24.0f; // px-3
}

ControlHit ControlKit::ChipButton(PixelSpace &Surface, const PlaneExtent &Extent, const char *Label, bool Active,
                                  const ControlPointer &Pointer, float Opacity) noexcept
{
    ControlHit Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;
    if (Active)
        Surface.FillRectangle(Extent, Faded(Palette().Primary, Opacity), 6.0f); // rounded-md, bg-white
    const ColorQuad Ink = Active ? Palette().PrimaryInk : (Hit.Hovered ? Palette().Text : Palette().TextDim);
    TextCentred(Surface, Extent, Faded(Ink, Opacity), Label, 12.0f);
    return Hit;
}

ControlHit ControlKit::RoundIconButton(PixelSpace &Surface, float CentreX, float CentreY,
                                       ControlCentreIconCategory Icon, const ControlPointer &Pointer,
                                       float Opacity) noexcept
{
    const float       R      = RoundIconDiameter * 0.5f;
    const PlaneExtent Extent = Spanning(CentreX - R, CentreY - R, RoundIconDiameter, RoundIconDiameter);
    ControlHit        Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;
    if (Hit.Hovered)
        FillCircle(Surface, CentreX, CentreY, R, Faded(HoverWash(), Opacity));                  // hover:bg-white/5
    OutlineCircle(Surface, CentreX, CentreY, R - 0.5f, Faded(Palette().Stroke, Opacity), 1.0f); // colors.panelBorder
    Glyph(Surface, CentreX - 8.0f, CentreY - 8.0f, 16.0f, Faded(Palette().TextDim, Opacity), Icon,
          2.0f); // colors.textMuted
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      SEGMENTED
//------------------------------------------------------------------------------------------------------------------------

ControlHit ControlKit::Segmented(PixelSpace &Surface, const PlaneExtent &Extent, const char *const *Labels,
                                 uint32_t Count, uint32_t Active, const ControlPointer &Pointer, uint32_t &OutIndex,
                                 float Opacity) noexcept
{
    ControlHit  Hit{};
    const float R = PillRadius(Extent);
    Surface.FillRectangle(Extent, Faded(Palette().Field, Opacity), R);
    OutlineRounded(Surface, Extent, Faded(Palette().Stroke, Opacity), R);

    // padding 4, gap 4, each button px-16 around its label
    float       X = Extent.MinimumX + 4.0f;
    const float H = Extent.Height() - 8.0f;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const float       W     = Surface.MeasureText(Labels[I], 13.0f).X + 32.0f;
        const PlaneExtent Cell  = Spanning(X, Extent.MinimumY + 4.0f, W, H);
        const bool        Hover = Over(Cell, Pointer);
        if (Hover)
        {
            Hit.Hovered = true;
            if (Pointer.Pressed)
                Hit.Pressed = true;
            if (Pointer.Released)
            {
                Hit.Clicked = true;
                OutIndex    = I;
            }
        }
        if (I == Active)
            Surface.FillRectangle(Cell, Faded(Palette().Primary, Opacity), H * 0.5f);
        TextCentred(Surface, Cell,
                    Faded(I == Active ? Palette().PrimaryInk : (Hover ? Palette().Text : Palette().TextDim), Opacity),
                    Labels[I], 13.0f);
        X += W + 4.0f;
    }
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        SLIDER
//------------------------------------------------------------------------------------------------------------------------

ControlHit ControlKit::Slider(PixelSpace &Surface, const PlaneExtent &Extent, float Minimum, float Maximum,
                              float Position, bool Dragging, const ControlPointer &Pointer, float &OutputPosition,
                              float Opacity, float RailHeight, float KnobDiameter) noexcept
{
    ControlHit        Hit{};
    const float       TrackHeight   = std::clamp(RailHeight, 4.0f, 48.0f);
    const float       ThumbDiameter = std::min(std::clamp(KnobDiameter, 4.0f, 48.0f), std::max(1.0f, Extent.Width()));
    const float       CentreY       = Extent.MinimumY + Extent.Height() * 0.5f;
    const PlaneExtent Track = Spanning(Extent.MinimumX, CentreY - TrackHeight * 0.5f, Extent.Width(), TrackHeight);
    // Hit band: the thumb's full height across the row.
    const PlaneExtent ContactExtent =
        Spanning(Extent.MinimumX, CentreY - ThumbDiameter * 0.5f - 2.0f, Extent.Width(), ThumbDiameter + 4.0f);

    Hit.Hovered  = Over(ContactExtent, Pointer);
    Hit.Pressed  = Hit.Hovered && Pointer.Pressed;
    Hit.Dragging = Dragging || Hit.Pressed;

    const float RangeLength = std::max(Maximum - Minimum, 1e-6f);
    float       Fraction    = std::clamp((Position - Minimum) / RangeLength, 0.0f, 1.0f);
    if (Hit.Dragging && Pointer.Down && Pointer.Enabled)
    {
        // Thumb centre tracks the pointer within [half-thumb, width − half-thumb] (browser range semantics).
        const float Usable = std::max(Extent.Width() - ThumbDiameter, 1.0f);
        Fraction           = std::clamp((Pointer.X - Extent.MinimumX - ThumbDiameter * 0.5f) / Usable, 0.0f, 1.0f);
        OutputPosition     = Minimum + Fraction * RangeLength;
    }

    // A FLAT bar with a knob, like the inspector's SliderPill: the fill runs flush inside the rail and the
    //    thumb rides on top with a drop shadow. (History: the fill used to sit in a 3 px groove inset into
    //    the rail; the Control Centre sliders were unified with the inspector pattern instead. Travel, hit
    //    band, and thumb sizes are unchanged — only the paint moved.)
    const float Radius = TrackHeight * 0.5f;
    Surface.FillRectangle(Track, Faded(Palette().SliderTrack, Opacity), Radius);

    const float ThumbCentreX = Extent.MinimumX + ThumbDiameter * 0.5f + Fraction * (Extent.Width() - ThumbDiameter);
    const float FillWidth    = std::max(ThumbCentreX - Track.MinimumX, 0.0f);
    if (FillWidth > 1.0f)
        Surface.FillRectangle(
            Spanning(Track.MinimumX, Track.MinimumY, std::min(FillWidth, Track.Width()), Track.Height()),
            Faded(Palette().SliderFill, Opacity), Radius);

    const float ThumbRadius =
        ThumbDiameter * 0.5f * (Hit.Dragging && Pointer.Down ? 1.12f : 1.0f); // held: × 1.12, like the inspector knob
    FillCircle(Surface, ThumbCentreX, CentreY + 1.0f, ThumbRadius, Faded(ColorQuad{0.0f, 0.0f, 0.0f, 0.5f}, Opacity));
    FillCircle(Surface, ThumbCentreX, CentreY, ThumbRadius, Faded(Palette().SliderThumb, Opacity));
    return Hit;
}

void ControlKit::ValuePill(PixelSpace &Surface, float X, float Y, const char *Number, const char *Unit, float Opacity,
                           float Width, float UnitWidth) noexcept
{
    const float H = ControlKitTokens::ControlHeight;
    // A pill narrower than its own unit cell has no number cell left to draw, and the seam arithmetic below goes
    //    negative. Clamped rather than asserted: a cramped panel should still render something readable.
    UnitWidth = std::clamp(UnitWidth, 12.0f, std::max(Width - 24.0f, 12.0f));
    // 🔴 TWO CELLS butted together inside one fully-rounded outline — a black number cell and a grey unit
    //    cell — NOT a chip floating inside a pill. From the mock, which is the spec:
    //
    //        .vpill { height:30px; border-radius:999px; overflow:hidden; border:1px solid --stroke }
    //        .vpill .num  { flex:1;    background:--field  (#000000) }
    //        .vpill .unit { width:36px; background:--inset (#1a1a1a); border-left:1px solid --stroke }
    //
    //    The cells fill the pill's full height and meet at a hard seam; the rounding is the PARENT's, applied
    //    by clipping, so the number cell is round on the left and square on the right and the unit cell is the
    //    reverse. Two earlier builds got this wrong in opposite directions — one drew a divider then painted
    //    over it, the other inset a chip and lost the seam entirely.
    //
    // ⚠️ PixelSpace has no per-corner radius, so the square edge is made by overdrawing the rounded cell with
    //    a plain rectangle in THE SAME COLOUR. That is safe precisely because it is the same colour; the
    //    original bug was overdrawing a divider with a different one. The divider is drawn last, over both.
    const float Radius = H * 0.5f;
    UnitWidth          = (Unit && Unit[0]) ? UnitWidth : 0.0f;
    const float Seam   = X + Width - UnitWidth;

    const PlaneExtent Whole    = Spanning(X, Y, Width, H);
    const PlaneExtent Num      = Spanning(X, Y, std::max(Width - UnitWidth, 1.0f), H);
    const PlaneExtent UnitCell = Spanning(Seam, Y, UnitWidth, H);

    // The grey unit colour underneath the whole pill, so the right-hand corners come out rounded for free.
    Surface.FillRectangle(Whole, Faded(Palette().Inset, Opacity), Radius);

    // The black number cell over it: rounded, then its right edge squared back with the same black.
    Surface.FillRectangle(Num, Faded(Palette().Field, Opacity), Radius);
    if (UnitWidth > 0.0f && Num.Width() > Radius)
    {
        Surface.FillRectangle(Spanning(X + Radius, Y, Num.Width() - Radius, H), Faded(Palette().Field, Opacity), 0.0f);

        // The unit cell's border-left. Drawn after both fills, so nothing can erase it this time.
        Surface.FillRectangle(Spanning(Seam, Y, 1.0f, H), Faded(Palette().Stroke, Opacity), 0.0f);
    }

    OutlineRounded(Surface, Whole, Faded(Palette().Stroke, Opacity), Radius);

    TextCentred(Surface, Num, Faded(Palette().Text, Opacity), Number, 13.5f);
    if (UnitWidth > 0.0f)
        TextCentred(Surface, UnitCell, Faded(Palette().TextFaint, Opacity), Unit, 11.5f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCROLLING
//------------------------------------------------------------------------------------------------------------------------

float ControlKit::AdvanceScroll(float Offset, float Wheel, float ContentHeight, float ViewHeight) noexcept
{
    // 40 px a click, which is roughly a row and a half — the same order as every desktop list.
    const float Travel = std::max(ContentHeight - ViewHeight, 0.0f);
    const float Moved  = Offset - Wheel * 40.0f; // wheel away from the user moves the content up
    return std::clamp(Moved, 0.0f, Travel);
}

void ControlKit::ScrollIndicator(PixelSpace &Surface, const PlaneExtent &View, float Offset, float ContentHeight,
                                 float Opacity) noexcept
{
    const float ViewH = View.Height();
    if (ContentHeight <= ViewH + 1.0f || ViewH <= 8.0f)
        return; // nothing to scroll, nothing to say

    const float Fraction = std::clamp(ViewH / ContentHeight, 0.06f, 1.0f);
    const float ThumbH   = std::max(ViewH * Fraction, 24.0f);
    const float Travel   = std::max(ContentHeight - ViewH, 1.0f);
    const float ThumbY   = View.MinimumY + std::clamp(Offset / Travel, 0.0f, 1.0f) * (ViewH - ThumbH);

    ColorQuad Ink = Palette().TextFaint;
    Ink.Alpha *= 0.55f;
    Surface.FillRectangle(Spanning(View.MaximumX - 5.0f, ThumbY, 3.0f, ThumbH), Faded(Ink, Opacity), 1.5f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       DROPDOWN
//------------------------------------------------------------------------------------------------------------------------

ControlHit ControlKit::Dropdown(PixelSpace &Surface, const PlaneExtent &Extent, const char *Current, bool Open,
                                const ControlPointer &Pointer, float Opacity) noexcept
{
    ControlHit Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;

    const float       H = Extent.Height(), R = H * 0.5f;
    const float       CaretW = 44.0f;
    const PlaneExtent Caret  = Spanning(Extent.MaximumX - CaretW, Extent.MinimumY, CaretW, H);
    Surface.FillRectangle(Extent, Faded(Palette().Field, Opacity), R);
    Surface.FillRectangle(Spanning(Caret.MinimumX - R, Extent.MinimumY, CaretW + R, H),
                          Faded(Hit.Hovered ? Palette().Raised : Palette().Inset, Opacity), R);
    Surface.FillRectangle(Spanning(Caret.MinimumX - R, Extent.MinimumY, R, H),
                          Faded(Palette().Field, Opacity)); // square the seam
    Surface.FillRectangle(Spanning(Caret.MinimumX, Extent.MinimumY, 1.0f, H), Faded(Palette().Stroke, Opacity));
    OutlineRounded(Surface, Extent, Faded(Palette().Stroke, Opacity), R);
    TextLeading(Surface, Extent, 16.0f, Faded(Palette().Text, Opacity), Current, 13.5f);
    GlyphCentred(Surface, Caret, 16.0f, Faded(Palette().TextDim, Opacity),
                 Open ? ControlCentreIconCategory::ChevronUp : ControlCentreIconCategory::ChevronDown);
    return Hit;
}

PlaneExtent ControlKit::DropdownMenuExtent(const PlaneExtent &ButtonExtent, uint32_t Count) noexcept
{
    return Spanning(ButtonExtent.MinimumX, ButtonExtent.MaximumY + ControlKitTokens::DropdownPaddingY, ButtonExtent.Width(),
                    2 * ControlKitTokens::DropdownPaddingY + Count * DropdownOptionHeight +
                        (Count > 0u ? (Count - 1u) * ControlKitTokens::DropdownRowGap : 0.0f));
}

ControlHit ControlKit::DropdownMenu(PixelSpace &Surface, const PlaneExtent &ButtonExtent, const char *const *Options,
                                    uint32_t Count, uint32_t Selected, const ControlPointer &Pointer,
                                    uint32_t &OutIndex, float Opacity) noexcept
{
    ControlHit        Hit{};
    const PlaneExtent Menu = DropdownMenuExtent(ButtonExtent, Count);
    Surface.FillRectangle(Menu, Faded(Palette().Field, Opacity), ControlKitTokens::DropdownCornerRadius);
    OutlineRounded(Surface, Menu, Faded(Palette().StrokeStrong, Opacity), ControlKitTokens::DropdownCornerRadius);
    float Y = Menu.MinimumY + ControlKitTokens::DropdownPaddingY;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        const PlaneExtent Row = Spanning(Menu.MinimumX + ControlKitTokens::DropdownPaddingX, Y,
                                         Menu.Width() - 2 * ControlKitTokens::DropdownPaddingX, DropdownOptionHeight);
        const bool        Hover = Over(Row, Pointer);
        if (Hover)
        {
            Hit.Hovered = true;
            if (Pointer.Pressed)
                Hit.Pressed = true;
            if (Pointer.Released)
            {
                Hit.Clicked = true;
                OutIndex    = I;
            }
        }
        if (I == Selected)
            Surface.FillRectangle(Row, Faded(Palette().Raised, Opacity), ControlKitTokens::DropdownRowRadius);
        else if (Hover)
            Surface.FillRectangle(Row, Faded(Palette().Inset, Opacity), ControlKitTokens::DropdownRowRadius);
        TextLeading(Surface, Row, 14.0f, Faded(I == Selected || Hover ? Palette().Text : Palette().TextDim, Opacity),
                    Options[I], 13.0f);
        Y += DropdownOptionHeight + ControlKitTokens::DropdownRowGap;
    }
    Hit.Hovered = Hit.Hovered || Over(Menu, Pointer);
    return Hit;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SECTIONS & ROWS
//------------------------------------------------------------------------------------------------------------------------

PlaneExtent ControlKit::SectionCard(PixelSpace &Surface, const PlaneExtent &Extent, float Radius,
                                    float Opacity) noexcept
{
    Surface.FillRectangle(Extent, Faded(Palette().Inset, Opacity), Radius);    // colors.inputBg
    OutlineRounded(Surface, Extent, Faded(Palette().Stroke, Opacity), Radius); // colors.panelBorder
    return PlaneExtent{Extent.MinimumX + SectionPadding, Extent.MinimumY + SectionPadding,
                       Extent.MaximumX - SectionPadding, Extent.MaximumY - SectionPadding};
}

float ControlKit::SectionHeading(PixelSpace &Surface, float X, float Y, float, const char *Title,
                                 const char *Description, ColorQuad TitleInk, ColorQuad MutedInk,
                                 float Opacity) noexcept
{
    const PlanePoint T = Surface.MeasureText(Title, 14.0f);
    Surface.Text(X, Y + (21.0f - T.Y) * 0.5f, Faded(TitleInk, Opacity), Title, 14.0f); // text-sm leading 20 + mb-1
    float Consumed = 20.0f + 4.0f;
    if (Description && *Description)
    {
        const PlanePoint D = Surface.MeasureText(Description, 12.0f);
        Surface.Text(X, Y + Consumed + (16.0f - D.Y) * 0.5f, Faded(MutedInk, Opacity), Description,
                     12.0f); // text-xs leading 16
        Consumed += 16.0f;
    }
    return Consumed;
}

ControlHit ControlKit::Swatch(PixelSpace &Surface, float Cx, float Cy, ColorQuad Colour, bool Selected, bool RingStyle,
                              const ControlPointer &Pointer, float Opacity) noexcept
{
    const float       R      = SwatchDiameter * 0.5f;
    const PlaneExtent Extent = Spanning(Cx - R, Cy - R, SwatchDiameter, SwatchDiameter);
    ControlHit        Hit{};
    Hit.Hovered       = Over(Extent, Pointer);
    Hit.Pressed       = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked       = Hit.Hovered && Pointer.Released;
    const float Scale = Hit.Hovered && RingStyle ? 1.1f : 1.0f; // Notch accent: hover:scale-110
    FillCircle(Surface, Cx, Cy, R * Scale, Faded(Colour, Opacity));
    if (Selected)
    {
        if (RingStyle)
        {
            OutlineCircle(Surface, Cx, Cy, R + 4.0f, Faded(Colour, Opacity),
                          2.0f); // inset-[-4px] border-2 in the colour
            FillCircle(Surface, Cx, Cy, 4.0f,
                       Faded(Palette().Field, Opacity)); // 2×2 → w-2 h-2 dot in the canvas colour (bg-black)
        }
        else
            OutlineCircle(Surface, Cx, Cy, R - 1.0f, Faded(Palette().Primary, Opacity), 2.0f); // border-2 border-white
    }
    return Hit;
}

PlaneExtent ControlKit::ControlRow(PixelSpace &Surface, float X, float Y, float Width, const char *Label,
                                   ColorQuad LabelInk, float Opacity) noexcept
{
    const PlaneExtent Row = Spanning(X, Y, Width, ControlKitTokens::ControlHeight);
    TextLeading(Surface, Spanning(X, Y, ControlKitTokens::LabelWidth, ControlKitTokens::ControlHeight), 0.0f,
                Faded(LabelInk, Opacity), Label, 13.5f);
    return PlaneExtent{X + ControlKitTokens::LabelWidth + ControlKitTokens::RowGap, Y, Row.MaximumX, Row.MaximumY};
}

ControlHit ControlKit::TextEntry(PixelSpace &Surface, const PlaneExtent &Extent, TextEntryState &Entry,
                                 const ControlPointer &Pointer, float FontSize, float Opacity) noexcept
{
    ControlHit Hit{};
    Hit.Hovered = Over(Extent, Pointer);
    Hit.Pressed = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked = Hit.Hovered && Pointer.Released;

    const float     R    = 6.0f;
    const float     PadX = 6.0f;
    const ColorQuad Edge = Entry.Active ? Palette().Highlight : Palette().Stroke;

    Surface.FillRectangle(Extent, Faded(Palette().Field, Opacity), R);
    OutlineRounded(Surface, Extent, Faded(Edge, Opacity), R, Entry.Active ? 1.5f : 1.0f);

    // Everything below is clipped to the field: a long name must scroll inside its box, never paint over the
    //    controls beside it.
    Surface.PushClip(Extent);

    const float TextY   = Extent.MinimumY + (Extent.Height() - FontSize) * 0.5f;
    const float CaretPx = Surface
                              .MeasureText(
                                  [&]
                                  {
                                      static char Head[TextEntryState::Capacity];
                                      for (uint32_t I = 0u; I < Entry.Caret; ++I)
                                          Head[I] = Entry.Text[I];
                                      Head[Entry.Caret] = '\0';
                                      return Head;
                                  }(),
                                  FontSize)
                              .X;

    // Keep the caret inside the visible span, scrolling only as far as needed in either direction.
    const float Inner = Extent.Width() - PadX * 2.0f;
    if (CaretPx - Entry.ScrollX > Inner)
        Entry.ScrollX = CaretPx - Inner;
    if (CaretPx - Entry.ScrollX < 0.0f)
        Entry.ScrollX = CaretPx;
    if (Entry.ScrollX < 0.0f)
        Entry.ScrollX = 0.0f;

    const float OriginX = Extent.MinimumX + PadX - Entry.ScrollX;

    if (Entry.Active && Entry.HasSelection())
    {
        static char    Head[TextEntryState::Capacity];
        const uint32_t From = Entry.SelectionStart(), To = Entry.SelectionEnd();
        for (uint32_t I = 0u; I < From; ++I)
            Head[I] = Entry.Text[I];
        Head[From]         = '\0';
        const float FromPx = Surface.MeasureText(Head, FontSize).X;
        for (uint32_t I = 0u; I < To; ++I)
            Head[I] = Entry.Text[I];
        Head[To]         = '\0';
        const float ToPx = Surface.MeasureText(Head, FontSize).X;
        ColorQuad   Wash = Palette().Highlight;
        Wash.Alpha *= 0.35f * Opacity;
        Surface.FillRectangle(Spanning(OriginX + FromPx, Extent.MinimumY + 3.0f, ToPx - FromPx, Extent.Height() - 6.0f),
                              Wash, 2.0f);
    }

    Surface.Text(OriginX, TextY, Faded(Palette().Text, Opacity), Entry.Text, FontSize);

    // Blink at the kit period, always visible for the half-cycle right after a keystroke because Insert() resets
    //    the phase — a caret that happens to be dark while you type reads as dropped input.
    if (Entry.Active)
    {
        const float Phase =
            Entry.BlinkPhase -
            static_cast<float>(static_cast<int>(Entry.BlinkPhase / CaretBlinkSeconds)) * CaretBlinkSeconds;
        if (Phase < CaretBlinkSeconds * 0.5f)
            Surface.FillRectangle(Spanning(OriginX + CaretPx, Extent.MinimumY + 3.0f, 1.5f, Extent.Height() - 6.0f),
                                  Faded(Palette().Text, Opacity));
    }

    Surface.PopClip();
    return Hit;
}

} // namespace Frontier
