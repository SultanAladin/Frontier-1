/*==========================================================================================================================================
                                                         CONFIGURATIONINSPECTOR.CPP
==========================================================================================================================================*/
// 🧩 Reuse the reference form geometry for functional input configuration and interface alerts.

#include "ConfigurationInspector.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{
namespace
{
constexpr float FieldHeight   = 40;
constexpr float FieldRadius   = 8;
constexpr float LabelSize     = 13;
constexpr float FormLabelCol  = 160;
constexpr float FormRowPadY   = 16;
constexpr float FormLabelSize = 14;
constexpr float FormRowHeight = 56;
constexpr float AlertsHeadTop = 32;
constexpr float AlertsHeadGap = 16;

ColorQuad Faded(ColorQuad Colour, float Opacity) noexcept
{
    Colour.Alpha *= Opacity;
    return Colour;
}

void ConstructField(PixelSpace &Surface, const PlaneExtent &Extent, float Opacity) noexcept
{
    Surface.FillRectangle(Extent, Faded(ControlKit::Palette().Field, Opacity), FieldRadius);
    ControlKit::OutlineRounded(Surface, Extent, Faded(ControlKit::Palette().StrokeStrong, Opacity), FieldRadius);
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 INPUT PREFERENCES AND CAPTURE
//------------------------------------------------------------------------------------------------------------------------

void InputInspector::InitializePreferences(const InputPreferences &Preferences) noexcept
{
    Applied = Preferences;
    Draft   = Preferences;
    ++Revision;
    ResetInteraction();
}

void InputInspector::FinalizePreferences() noexcept
{
    Applied = Draft;
    ++Revision;
    ResetInteraction();
}

void InputInspector::ResetDraft() noexcept
{
    Draft = Applied;
    ResetInteraction();
}

void InputInspector::ResetDefaults() noexcept
{
    Draft = InputPreferences{};
    ResetInteraction();
}

void InputInspector::ResetInteraction() noexcept
{
    ProfileOpen     = false;
    ShortcutContact = -1;
    DraggingSlider  = false;
    ResetKeyCapture();
}

void InputInspector::ResetKeyCapture() noexcept
{
    CaptureSlot = -1;
    Diagnostic.clear();
}

bool InputInspector::ConfigureCapturedChord(const InputKeyChord &Chord) noexcept
{
    if (CaptureSlot < 0 || CaptureSlot >= static_cast<int>(Draft.ShortcutCount))
        return false;
    auto Candidate                   = Draft;
    Candidate.Shortcuts[CaptureSlot] = Chord;
    Candidate.CustomShortcuts        = true;
    if (!InputChordTable::Validate(Candidate, Diagnostic))
        return false;
    Draft = Candidate;
    ResetKeyCapture();
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INPUT FORM GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

float InputInspector::ConstructInputLayout(PixelSpace &Surface, const PlaneExtent &Body, float ScrollY,
                                           const ControlPointer &Pointer, float Opacity) noexcept
{
    const auto &Palette = ControlKit::Palette();
    const float Left    = Body.MinimumX + 16;
    const float Width   = std::max(1.0f, std::min(Body.Width() - 32, 768.0f));
    float       CursorY = Body.MinimumY - ScrollY;
    auto        Local   = Pointer;
    if (ProfileOpen)
        Local.Enabled = false;
    auto ControlRow = [&](const char *Label)
    {
        Surface.Text(Left, CursorY + 12, Faded(Palette.TextDim, Opacity), Label, LabelSize);
        if (Width < 500)
        {
            CursorY += 32;
            return Spanning(Left, CursorY, Width, FieldHeight);
        }
        return Spanning(Left + 156, CursorY, Width - 156, FieldHeight);
    };
    auto Section = [&](const char *Label)
    {
        CursorY += 24;
        Surface.FillRectangle(Spanning(Left, CursorY, Width, 1), Faded(Palette.Divider, Opacity));
        CursorY += 24;
        Surface.Text(Left, CursorY + 4, Faded(Palette.Text, Opacity), Label, 14.5f);
        CursorY += 40;
    };
    ProfileExtent = ControlRow("Shortcut profile");
    ConstructField(Surface, ProfileExtent, Opacity);
    ControlKit::TextLeading(Surface, ProfileExtent, 16, Faded(Palette.Text, Opacity),
                            Draft.CustomShortcuts ? "Custom" : "Editor Default", LabelSize);
    ControlKit::GlyphCentred(Surface, Spanning(ProfileExtent.MaximumX - 32, ProfileExtent.MinimumY, 24, 40), 16,
                             Faded(Palette.TextDim, Opacity), ControlCentreIconCategory::ChevronDown);
    if (ControlKit::Over(ProfileExtent, Local) && Local.Released)
    {
        ProfileOpen = true;
        ResetKeyCapture();
    }
    CursorY += 52;
    const auto SensitivityRow = ControlRow("Mouse Sensitivity");
    char       Number[16];
    std::snprintf(Number, sizeof(Number), "%.0f", static_cast<double>(Draft.MouseSensitivity));
    ControlKit::ValuePill(Surface, SensitivityRow.MinimumX, CursorY, Number, "%", Opacity);
    const bool  Stacked    = SensitivityRow.Width() < 300;
    const float SliderLeft = SensitivityRow.MinimumX + ControlKit::ValuePillWidth + ControlKitTokens::RowGap;
    SensitivityExtent      = Stacked
                                 ? Spanning(SensitivityRow.MinimumX, CursorY + 48, SensitivityRow.Width(), 40)
                                 : Spanning(SliderLeft, CursorY, std::max(1.0f, SensitivityRow.MaximumX - SliderLeft), 40);
    float      Sensitivity = Draft.MouseSensitivity;
    const auto SliderHit   = ControlKit::Slider(Surface, SensitivityExtent, 0, 100, Sensitivity, DraggingSlider, Local,
                                                Sensitivity, Opacity);
    if (SliderHit.Pressed)
        DraggingSlider = true;
    if (DraggingSlider)
        Draft.MouseSensitivity = std::round(Sensitivity);
    if (!Pointer.Down)
        DraggingSlider = false;
    CursorY += Stacked ? 100 : 52;

    Section("Custom Shortcuts");
    CustomSwitchExtent = Spanning(Left + Width - ControlKit::SwitchWidth, CursorY - 40, ControlKit::SwitchWidth,
                                  ControlKit::SwitchHeight);
    if (ControlKit::Switch(Surface, CustomSwitchExtent.MinimumX, CustomSwitchExtent.MinimumY, Draft.CustomShortcuts,
                           Local, Opacity)
            .Clicked)
    {
        Draft.CustomShortcuts = !Draft.CustomShortcuts;
        ResetKeyCapture();
    }
    for (uint32_t Index = 0; Index < InputPreferences::ShortcutCount; ++Index)
    {
        const auto Command     = static_cast<InputCommandCategory>(Index);
        const auto Extent      = ControlRow(InputChordTable::QueryCommandLabel(Command));
        ShortcutExtents[Index] = Extent;
        ConstructField(Surface, Extent, Opacity);
        const bool Capturing = CaptureSlot == static_cast<int>(Index);
        const auto Label     = Capturing ? std::string("Press a key (Esc cancels)")
                                         : InputChordTable::QueryChordLabel(Draft.QueryShortcut(Command));
        Surface.PushClip(Extent);
        ControlKit::TextLeading(Surface, Extent, 16,
                                Faded(Draft.CustomShortcuts ? Palette.Text : Palette.TextFaint, Opacity), Label.c_str(),
                                LabelSize);
        Surface.PopClip();
        if (Capturing)
            ControlKit::OutlineRounded(Surface, Extent, Faded(Palette.Accent, Opacity), FieldRadius);
        if (Local.Pressed && ControlKit::Over(Extent, Local))
            ShortcutContact = static_cast<int>(Index);
        if (Draft.CustomShortcuts && ShortcutContact == static_cast<int>(Index) && ControlKit::Over(Extent, Local) &&
            Local.Released)
        {
            CaptureSlot = static_cast<int>(Index);
            Diagnostic.clear();
        }
        CursorY += 52;
    }
    if (!Pointer.Down)
        ShortcutContact = -1;
    Section("Advanced Controls");
    AdvancedSwitchExtent = Spanning(Left + Width - ControlKit::SwitchWidth, CursorY - 40, ControlKit::SwitchWidth,
                                    ControlKit::SwitchHeight);
    if (ControlKit::Switch(Surface, AdvancedSwitchExtent.MinimumX, AdvancedSwitchExtent.MinimumY,
                           Draft.AdvancedControls, Local, Opacity)
            .Clicked)
        Draft.AdvancedControls = !Draft.AdvancedControls;
    const auto InversionRow = ControlRow("Invert Y-Axis");
    InvertSwitchExtent      = Spanning(InversionRow.MaximumX - ControlKit::SwitchWidth, InversionRow.MinimumY + 7,
                                       ControlKit::SwitchWidth, ControlKit::SwitchHeight);
    auto AdvancedPointer    = Local;
    AdvancedPointer.Enabled = AdvancedPointer.Enabled && Draft.AdvancedControls;
    if (ControlKit::Switch(Surface, InvertSwitchExtent.MinimumX, InvertSwitchExtent.MinimumY, Draft.InvertPitch,
                           AdvancedPointer, Opacity * (Draft.AdvancedControls ? 1.0f : 0.4f))
            .Clicked)
        Draft.InvertPitch = !Draft.InvertPitch;
    CursorY += 56;
    char Preview[160];
    std::snprintf(Preview, sizeof(Preview), "Applied pointer response: dx %.1f / dy %.1f (UI preview)",
                  static_cast<double>(PointerDelta.X), static_cast<double>(PointerDelta.Y));
    Surface.Text(Left, CursorY, Faded(Palette.TextDim, Opacity), Preview, 12);
    CursorY += 24;
    Surface.Text(Left, CursorY, Faded(Palette.TextFaint, Opacity),
                 "Scene camera/tools are not hosted; drawer gestures stay 1:1.", 11);
    CursorY += 24;
    return CursorY - Body.MinimumY + ScrollY;
}

PlaneExtent InputInspector::QueryProfileOptionExtent(uint32_t Index) const noexcept
{
    return Index < 2 ? Spanning(ProfileExtent.MinimumX + 4, ProfileExtent.MaximumY + 10 + Index * 40,
                                ProfileExtent.Width() - 8, 40)
                     : PlaneExtent{};
}

void InputInspector::ConstructFloatingLayout(PixelSpace &Surface, const ControlPointer &Pointer, float Opacity) noexcept
{
    if (!ProfileOpen)
        return;
    const auto &Palette = ControlKit::Palette();
    const auto  Menu    = Spanning(ProfileExtent.MinimumX, ProfileExtent.MaximumY + 6, ProfileExtent.Width(), 88);
    ConstructField(Surface, Menu, Opacity);
    const char *Names[] = {"Editor Default", "Custom"};
    for (uint32_t Index = 0; Index < 2; ++Index)
    {
        const auto Row   = QueryProfileOptionExtent(Index);
        const bool Hover = ControlKit::Over(Row, Pointer);
        if (Hover)
            Surface.FillRectangle(Row, Faded(Palette.Selected, Opacity), 4);
        ControlKit::TextLeading(Surface, Row, 12, Faded(Palette.Text, Opacity), Names[Index], LabelSize);
        if (Hover && Pointer.Released)
        {
            Draft.CustomShortcuts = Index == 1;
            ProfileOpen           = false;
            return;
        }
    }
    if (Pointer.Pressed && !Menu.Encloses(Pointer.X, Pointer.Y) && !ProfileExtent.Encloses(Pointer.X, Pointer.Y))
        ProfileOpen = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                NOTIFICATION INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

NotificationInspector::NotificationInspector() noexcept
    : Applied{}, Draft{}, Revision(0u), DraggingSlider(false), ToggleExtents{}, HoldSliderExtent{}
{
}

ControlHit NotificationInspector::ConstructToggle(PixelSpace &Surface, const PlaneExtent &Extent, bool Enabled,
                                                  const ControlPointer &Pointer, float Opacity) noexcept
{
    ControlHit Hit{};
    Hit.Hovered                      = ControlKit::Over(Extent, Pointer);
    Hit.Pressed                      = Hit.Hovered && Pointer.Pressed;
    Hit.Clicked                      = Hit.Hovered && Pointer.Released;
    const ControlKitPalette &Palette = ControlKit::Palette();
    const ColorQuad          InactiveColour =
        Palette.LightSurface ? ColorQuad{0.0f, 0.0f, 0.0f, 0.10f} : ColorQuad{1.0f, 1.0f, 1.0f, 0.10f}; // bg-white/10
    Surface.FillRectangle(Extent, Faded(Enabled ? Palette.Accent : InactiveColour, Opacity),
                          12.0f);                                      // w-12 h-6 rounded-full
    const float KnobLeft = Extent.MinimumX + (Enabled ? 26.0f : 2.0f); // animate x 2 → 26
    ControlKit::FillCircle(Surface, KnobLeft + 10.0f, Extent.MinimumY + 2.0f + 10.0f, 10.0f,
                           Faded(ColorQuad{1.0f, 1.0f, 1.0f, 1.0f},
                                 Opacity)); // w-5 h-5 top-0.5 bg-white
    return Hit;
}

float NotificationInspector::ConstructNotificationLayout(PixelSpace &Surface, const PlaneExtent &Body, float ScrollY,
                                                         const ControlPointer &Pointer, float Opacity) noexcept
{
    // GenericSettingsModal: px-8 lg:px-12 py-8, max-w-3xl column of FormFieldRow
    // (grid 160px | 1fr, py-4, border-b last:0).
    const ControlKitPalette &Palette = ControlKit::Palette();
    const float              Left    = Body.MinimumX + 16.0f;
    const float              Width   = std::min(Body.Width() - 32.0f, 768.0f);
    float                    CursorY = Body.MinimumY - ScrollY;

    auto Row = [&](uint32_t Index, const char *Label, bool &Enabled, bool Last)
    {
        const PlanePoint TextExtent = Surface.MeasureText(Label, FormLabelSize);
        Surface.Text(Left, CursorY + (FormRowHeight - TextExtent.Y) * 0.5f, Faded(Palette.Text, Opacity), Label,
                     FormLabelSize); // text-sm tracking-tight colors.text
        ToggleExtents[Index] = Spanning(Left + Width - 48.0f, CursorY + FormRowPadY, 48.0f,
                                        24.0f); // flex justify-end w-full
        if (ConstructToggle(Surface, ToggleExtents[Index], Enabled, Pointer, Opacity).Clicked)
            Enabled = !Enabled;
        CursorY += FormRowHeight;
        if (!Last)
            Surface.FillRectangle(Spanning(Left, CursorY - 0.5f, Width, 1.0f),
                                  Faded(Palette.Divider, Opacity)); // border-b colors.divider
    };

    Row(0u, "Show FPS Overlay", Draft.ShowFrameRateOverlay, true);
    CursorY += AlertsHeadTop;
    Surface.Text(Left, CursorY, Faded(Palette.Text, Opacity), "Interface alerts", FormLabelSize);
    CursorY += Surface.MeasureText("Interface alerts", FormLabelSize).Y + AlertsHeadGap;
    Row(1u, "Preferences Applied", Draft.PreferencesApplied, false);
    Row(2u, "Preference Errors", Draft.PreferenceErrors, true);

    // Engine addition (flagged): toast dwell. Same FormFieldRow grid, value pill
    // + thin kit slider.
    {
        CursorY += AlertsHeadTop;
        Surface.Text(Left, CursorY, Faded(Palette.Text, Opacity), "Toasts", FormLabelSize);
        CursorY += Surface.MeasureText("Toasts", FormLabelSize).Y + AlertsHeadGap;
        const PlanePoint TextExtent = Surface.MeasureText("Hold Duration", FormLabelSize);
        Surface.Text(Left, CursorY + (FormRowHeight - TextExtent.Y) * 0.5f, Faded(Palette.Text, Opacity),
                     "Hold Duration", FormLabelSize);
        char DurationText[16];
        std::snprintf(DurationText, sizeof(DurationText), "%.1f", static_cast<double>(Draft.HoldSeconds));
        float      DurationTop = CursorY + (FormRowHeight - ControlKitTokens::ControlHeight) * 0.5f;
        const bool Stacked     = Width < 500;
        if (Stacked)
            DurationTop += 32;
        const float NumberX = Stacked ? Left : Left + FormLabelCol;
        ControlKit::ValuePill(Surface, NumberX, DurationTop, DurationText, "s", Opacity);
        const float SliderX = NumberX + ControlKit::ValuePillWidth + ControlKitTokens::RowGap;
        HoldSliderExtent    = Stacked ? Spanning(Left, DurationTop + 48, Width, ControlKitTokens::ControlHeight)
                                      : Spanning(SliderX, DurationTop, std::max(1.0f, Left + Width - SliderX),
                                                 ControlKitTokens::ControlHeight);
        if (Stacked)
            CursorY += 80;
        float            Duration = Draft.HoldSeconds;
        const ControlHit Hit      = ControlKit::Slider(Surface, HoldSliderExtent, 1.0f, 10.0f, Duration, DraggingSlider,
                                                       Pointer, Duration, Opacity);
        if (Hit.Pressed)
            DraggingSlider = true;
        if (DraggingSlider && !Pointer.Down)
            DraggingSlider = false;
        if (DraggingSlider)
            Draft.HoldSeconds = std::round(Duration * 2.0f) * 0.5f;
        CursorY += FormRowHeight;
    }

    return CursorY - (Body.MinimumY - ScrollY);
}

} // namespace Frontier
