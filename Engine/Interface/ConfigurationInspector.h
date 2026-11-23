/*==========================================================================================================================================
                                                          CONFIGURATIONINSPECTOR.H
==========================================================================================================================================*/
// 🧩 Reusable input configuration, chord capture and interface-notification layouts.

#pragma once

#include "ConfigurationStructure.h"
#include "ControlKit.h"

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   INPUT INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class InputInspector
{
  public:
    [[nodiscard]] const InputPreferences &QueryApplied() const noexcept
    {
        return Applied;
    }
    [[nodiscard]] const InputPreferences &QueryDraft() const noexcept
    {
        return Draft;
    }
    [[nodiscard]] bool QueryDirty() const noexcept
    {
        return !(Draft == Applied);
    }
    [[nodiscard]] bool QueryDefault() const noexcept
    {
        return Draft == InputPreferences{};
    }
    [[nodiscard]] uint32_t QueryRevision() const noexcept
    {
        return Revision;
    }
    [[nodiscard]] bool QueryMenuOpen() const noexcept
    {
        return ProfileOpen;
    }
    [[nodiscard]] bool QueryDragging() const noexcept
    {
        return DraggingSlider;
    }
    [[nodiscard]] bool QueryKeyCapture() const noexcept
    {
        return CaptureSlot >= 0;
    }
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept
    {
        return Diagnostic;
    }
    [[nodiscard]] PlanePoint QueryPointerDelta() const noexcept
    {
        return PointerDelta;
    }

    void ConfigureDraft(const InputPreferences &Preferences) noexcept
    {
        Draft = Preferences;
    }
    void ConfigurePointerDelta(PlanePoint Displacement) noexcept
    {
        PointerDelta = Applied.EvaluatePointerDelta(Displacement);
    }
    void               InitializePreferences(const InputPreferences &Preferences) noexcept;
    void               FinalizePreferences() noexcept;
    void               ResetDraft() noexcept;
    void               ResetDefaults() noexcept;
    void               ResetInteraction() noexcept;
    void               ResetKeyCapture() noexcept;
    [[nodiscard]] bool ConfigureCapturedChord(const InputKeyChord &Chord) noexcept;

    float ConstructInputLayout(
        PixelSpace           &Surface,
        const PlaneExtent    &Body,
        float                ScrollY,
        const ControlPointer &Pointer,
        float                Opacity) noexcept;
    void  ConstructFloatingLayout(PixelSpace &Surface, const ControlPointer &Pointer, float Opacity) noexcept;
    [[nodiscard]] PlaneExtent QueryProfileDropdownExtent() const noexcept
    {
        return ProfileExtent;
    }
    [[nodiscard]] PlaneExtent QueryProfileOptionExtent(uint32_t Index) const noexcept;
    [[nodiscard]] PlaneExtent QuerySensitivitySliderExtent() const noexcept
    {
        return SensitivityExtent;
    }
    [[nodiscard]] PlaneExtent QueryCustomShortcutsSwitchExtent() const noexcept
    {
        return CustomSwitchExtent;
    }
    [[nodiscard]] PlaneExtent QueryAdvancedSwitchExtent() const noexcept
    {
        return AdvancedSwitchExtent;
    }
    [[nodiscard]] PlaneExtent QueryInvertSwitchExtent() const noexcept
    {
        return InvertSwitchExtent;
    }
    [[nodiscard]] PlaneExtent QueryShortcutExtent(uint32_t Index) const noexcept
    {
        return Index < ShortcutExtents.size() ? ShortcutExtents[Index] : PlaneExtent{};
    }

  private:
    InputPreferences Applied;
    InputPreferences Draft;
    uint32_t         Revision        = 0;
    bool             ProfileOpen     = false;
    bool             DraggingSlider  = false;
    int              CaptureSlot     = -1;
    int              ShortcutContact = -1;
    std::string      Diagnostic;
    PlanePoint       PointerDelta;
    PlaneExtent      ProfileExtent, SensitivityExtent, CustomSwitchExtent, AdvancedSwitchExtent, InvertSwitchExtent;
    std::array<PlaneExtent, InputPreferences::ShortcutCount> ShortcutExtents{};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                NOTIFICATION INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

class NotificationInspector
{
  public:
    NotificationInspector() noexcept;

    [[nodiscard]] const NotificationPreferences &QueryApplied() const noexcept
    {
        return Applied;
    }
    [[nodiscard]] const NotificationPreferences &QueryDraft() const noexcept
    {
        return Draft;
    }
    [[nodiscard]] bool QueryDirty() const noexcept
    {
        return !(Draft == Applied);
    }
    [[nodiscard]] bool QueryDefault() const noexcept
    {
        return Draft == NotificationPreferences{};
    }
    [[nodiscard]] uint32_t QueryRevision() const noexcept
    {
        return Revision;
    }

    void ConfigureDraft(const NotificationPreferences &Preferences) noexcept { Draft = Preferences; }

    void FinalizePreferences() noexcept
    {
        Applied = Draft;
        ++Revision;
    }
    void InitializePreferences(const NotificationPreferences &Persisted) noexcept
    {
        Applied = Persisted;
        Draft   = Persisted;
        ++Revision;
    }
    void ResetDraft() noexcept
    {
        Draft          = Applied;
        DraggingSlider = false;
    }
    void ResetDefaults() noexcept
    {
        Draft = NotificationPreferences{};
    }
    // The dashboard "FPS Overlay" tile and this page edit the same flag: the host
    // mirrors the tile into both copies.
    void ConfigureFrameRateOverlay(bool On) noexcept
    {
        Applied.ShowFrameRateOverlay = On;
        Draft.ShowFrameRateOverlay   = On;
    }
    [[nodiscard]] bool QueryDragging() const noexcept
    {
        return DraggingSlider;
    }

    float ConstructNotificationLayout(
        PixelSpace           &Surface,
        const PlaneExtent    &Body,
        float                ScrollY,
        const ControlPointer &Pointer,
        float                Opacity) noexcept;

    static constexpr uint32_t RowCount = 4u; // FPS, preference applied/errors, hold duration
    [[nodiscard]] PlaneExtent QueryToggleExtent(uint32_t Row) const noexcept
    {
        return Row < 3u ? ToggleExtents[Row] : PlaneExtent{};
    }
    [[nodiscard]] PlaneExtent QueryHoldSliderExtent() const noexcept
    {
        return HoldSliderExtent;
    }

  private:
    // Notch FormToggle: w-12 h-6 rounded-full, accentColor when on / white-10
    // off, knob w-5 h-5 bg-white top-0.5, x 2 → 26.
    static ControlHit ConstructToggle(
        PixelSpace           &Surface,
        const PlaneExtent    &Extent,
        bool                 On,
        const ControlPointer &Pointer,
        float                Opacity) noexcept;

    NotificationPreferences Applied;
    NotificationPreferences Draft;
    uint32_t                Revision;
    bool                    DraggingSlider;
    PlaneExtent             ToggleExtents[3];
    PlaneExtent             HoldSliderExtent;
};

} // namespace Frontier
