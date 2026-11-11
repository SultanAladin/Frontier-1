/*============================================================================================================================================
                                                       CONTROLCENTREPANEL.H
============================================================================================================================================*/
// 🧩 Top-notch pull-down Control Centre, Appearance/Input/Notifications/Workspace settings, toast overlay, and modal dialogues

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeCatalog.h"
#include "Engine/Components/Appearance/TypefaceCatalog.h"
#include "Engine/Components/Controls/BooleanSwitch.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ConfirmationDialogue.h"
#include "Engine/Components/Controls/ControlStyle.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/NotificationSequence.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Controls/ThemePreviewTile.h"
#include "Engine/Components/Motion/SpringIntegrator.h"
#include "Engine/Components/Notch/PanelExpansionTab.h"
#include "Engine/Configuration/InputSettings.h"
#include "Engine/Configuration/ProjectSpaceArchive.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ControlCentrePage : uint8_t
{
    QuickControls = 0, // [-] - Top-level quick tiles and notification stream
    Settings      = 1, // [-] - 4-row settings directory
    Appearance    = 2, // [-] - Display scale, Typography, and Theme inspector
    Input         = 3, // [-] - Shortcut profile, chords, and pointer sensitivity
    Notifications = 4, // [-] - Rate overlay and interface alert preferences
    Workspace     = 5, // [-] - Detached OS window toggle and layout reset
};

enum class AppearanceSectionTab : uint8_t
{
    Display    = 0, // [-] - Interface scaling section
    Typography = 1, // [-] - Typeface strip, specimen playground, and 6-slot type scale
    Theme      = 2, // [-] - 8 color schemes, corner radius, accents, and semantic swatches
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class ControlCentrePanel
{
public:
    ControlCentrePanel() noexcept;

    void AttachTypefaces(TypefaceCatalog* Catalog) noexcept;
    bool AttachProjectSpace(const std::filesystem::path& ArchivePath, std::string* OutDiagnostic = nullptr) noexcept;
    bool SaveToProjectSpace(std::string* OutDiagnostic = nullptr) noexcept;

    void ConfigureWorkspaceCallbacks(
        std::function<void(bool)> OnDetachedWindowsChanged,
        std::function<void()>     OnResetLayoutRequested) noexcept;

    void ConfigureOpen(bool OpenRequested, bool Immediate = false) noexcept;
    void ActivatePage(ControlCentrePage Page, bool Immediate = false) noexcept;
    void ActivateAppearanceTab(AppearanceSectionTab Tab) noexcept;

    void ApplyDraftAppearanceImmediate() noexcept;
    void ApplyDraftInputImmediate() noexcept;
    void ApplyDraftNotificationsImmediate() noexcept;

    bool OpenConfirmationModal(const ConfirmationSpecification& Specification);

    void Render(Rectangle2D HostBoundsPhysical, double Δt, float RefreshRateHertz = 60.0f);

    [[nodiscard]] bool                           QueryOpen() const noexcept;
    [[nodiscard]] float                          QueryShadeProgress() const noexcept;
    [[nodiscard]] ControlCentrePage              QueryActivePage() const noexcept;
    [[nodiscard]] AppearanceSectionTab           QueryAppearanceTab() const noexcept;
    [[nodiscard]] float                          QueryInterfaceScaleFactor() const noexcept;
    [[nodiscard]] const ThemeCatalog&            QueryThemeCatalog() const noexcept;
    [[nodiscard]] const AppearanceSettings&      QueryAppliedAppearance() const noexcept;
    [[nodiscard]] AppearanceSettings&            AccessDraftAppearance() noexcept;
    [[nodiscard]] const InputPreferences&        QueryAppliedInput() const noexcept;
    [[nodiscard]] InputPreferences&              AccessDraftInput() noexcept;
    [[nodiscard]] const NotificationPreferences& QueryAppliedNotifications() const noexcept;
    [[nodiscard]] NotificationPreferences&       AccessDraftNotifications() noexcept;
    [[nodiscard]] NotificationSequence&          AccessNotificationStream() noexcept;
    [[nodiscard]] ConfirmationDialogue&          AccessConfirmationModal() noexcept;
    [[nodiscard]] bool                           QueryDetachedWindowsAllowed() const noexcept;

    void ConfigureDetachedWindowsAllowed(bool Allowed) noexcept;
    void ConfigureScrollOffset(float Offset) noexcept;
    void BeginInputShortcutCapture(uint32_t SlotIndex) noexcept;

private:
    enum class PendingDialogueIntent : uint8_t
    {
        None                      = 0,
        ConfirmApplyAppearance    = 1,
        ConfirmApplyInput         = 2,
        ConfirmApplyNotifications = 3,
        ConfirmDiscardChanges     = 4,
        ExternalCustom            = 5,
    };

    void               SyncAppliedControlColors() noexcept;
    void               RequestBackNavigation() noexcept;
    [[nodiscard]] bool QueryCurrentPageDirty() const noexcept;
    void               ResolveConfirmationResponse(DialogueResponse Response);

    void DrawPageContents(
        PixelSpace&           Surface,
        ControlCentrePage     Page,
        Rectangle2D           ContainerBounds,
        const ControlPointer& Pointer,
        float                 Opacity);

    void DrawQuickControlsPage(
        PixelSpace&           Surface,
        Rectangle2D           ContainerBounds,
        const ControlPointer& Pointer,
        float                 Opacity);

    void DrawSettingsDirectoryPage(
        PixelSpace&           Surface,
        Rectangle2D           ContainerBounds,
        const ControlPointer& Pointer,
        float                 Opacity);

    void DrawSubPageContainer(
        PixelSpace&           Surface,
        ControlCentrePage     Page,
        Rectangle2D           ContainerBounds,
        const ControlPointer& Pointer,
        float                 Opacity);

    float DrawAppearanceDisplayBody(
        PixelSpace&           Surface,
        Rectangle2D           BodyBounds,
        float                 ScrollY,
        const ControlPointer& Pointer,
        float                 Opacity);

    float DrawAppearanceTypographyBody(
        PixelSpace&           Surface,
        Rectangle2D           BodyBounds,
        float                 ScrollY,
        const ControlPointer& Pointer,
        float                 Opacity);

    float DrawAppearanceThemeBody(
        PixelSpace&           Surface,
        Rectangle2D           BodyBounds,
        float                 ScrollY,
        const ControlPointer& Pointer,
        float                 Opacity);

    float DrawInputSettingsBody(
        PixelSpace&           Surface,
        Rectangle2D           BodyBounds,
        float                 ScrollY,
        const ControlPointer& Pointer,
        float                 Opacity);

    void DrawInputProfileOverlay(
        PixelSpace&           Surface,
        const ControlPointer& Pointer,
        float                 Opacity);

    float DrawNotificationsBody(
        PixelSpace&           Surface,
        Rectangle2D           BodyBounds,
        float                 ScrollY,
        const ControlPointer& Pointer,
        float                 Opacity);

    float DrawWorkspaceBody(
        PixelSpace&           Surface,
        Rectangle2D           BodyBounds,
        float                 ScrollY,
        const ControlPointer& Pointer,
        float                 Opacity);

    TypefaceCatalog*          Typefaces                 = nullptr;                        // [-]   - Attached font archive catalog
    std::filesystem::path     AttachedArchivePath       {};                               // [-]   - Active .projectspace path
    ThemeCatalog              AppliedTheme              {};                               // [-]   - Committed workspace theme
    AppearanceSettings        AppliedAppearance         {};                               // [-]   - Committed appearance preferences
    AppearanceSettings        DraftAppearance           {};                               // [-]   - Unsaved appearance edits
    InputPreferences          AppliedInput              {};                               // [-]   - Committed input preferences
    InputPreferences          DraftInput                {};                               // [-]   - Unsaved input edits
    NotificationPreferences   AppliedNotifications      {};                               // [-]   - Committed notification preferences
    NotificationPreferences   DraftNotifications        {};                               // [-]   - Unsaved notification edits
    bool                      DetachedWindowsAllowed    = false;                          // [-]   - Multi-window OS tear-off toggle
    PanelExpansionTab         Notch                     {};                               // [-]   - Top pull-down trapezoidal notch
    SpringIntegrator          SheetSprings              {};                               // [-]   - Width/height sheet spring solver
    uint32_t                  WidthSpringAxis           = 0u;                             // [idx] - Sheet width spring axis index
    uint32_t                  HeightSpringAxis          = 0u;                             // [idx] - Sheet height spring axis index
    bool                      SheetGeometrySeeded       = false;                          // [-]   - Initial spring seeding guard
    ControlCentrePage         ActivePage                = ControlCentrePage::QuickControls;// [-]  - Active drawer page
    AppearanceSectionTab      ActiveAppearanceTab       = AppearanceSectionTab::Display;  // [-]   - Active Appearance tab
    float                     ScrollOffset              = 0.0f;                           // [px]  - Sub-page vertical scroll offset
    float                     LastContentHeight         = 0.0f;                           // [px]  - Measured sub-page content height
    float                     StreamScrollOffset        = 0.0f;                           // [px]  - Notification stream scroll offset
    float                     FontStripScroll           = 0.0f;                           // [px]  - Typeface strip horizontal offset
    float                     FontStripTarget           = 0.0f;                           // [px]  - Typeface strip target offset
    float                     FontStripContentWidth     = 0.0f;                           // [px]  - Total horizontal strip width
    float                     FontStripViewWidth        = 0.0f;                           // [px]  - Visible horizontal strip width
    int                       ActiveSliderOrdinal       = -1;                             // [idx] - Dragged slider identifier (-1 = none)
    bool                      InputProfileSelectorOpen  = false;                          // [-]   - Profile dropdown open toggle
    Rectangle2D               InputProfileBounds        {};                               // [px]  - Profile dropdown button bounds
    int                       CaptureShortcutSlot       = -1;                             // [idx] - Active chord capture slot (-1 = none)
    int                       PressedShortcutSlot       = -1;                             // [idx] - Pointer-down chord slot (-1 = none)
    Point2D                   PreviewPointerDelta       {};                               // [px]  - Transformed pointer delta preview
    std::string               StatusDiagnostic          {};                               // [-]   - Footer validation message
    NotificationSequence      Notifications             {};                               // [-]   - Toast & history sequence
    ConfirmationDialogue      Dialogue                  {};                               // [-]   - Modal confirmation dialogue
    PendingDialogueIntent     DialogueIntent            = PendingDialogueIntent::None;    // [-]   - Pending modal callback intent
    ControlCentrePage         DiscardTargetPage         = ControlCentrePage::Settings;    // [-]   - Target page after discard confirmation
    std::function<void(bool)> DetachedWindowsCallback   {};                               // [-]   - Host detached-window callback
    std::function<void()>     ResetLayoutCallback       {};                               // [-]   - Host layout-reset callback
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline void ControlCentrePanel::AttachTypefaces(TypefaceCatalog* Catalog) noexcept
{
    Typefaces = Catalog;
}

inline void ControlCentrePanel::ActivateAppearanceTab(AppearanceSectionTab Tab) noexcept
{
    ActiveAppearanceTab = Tab;
    ScrollOffset        = 0.0f;
}

inline bool ControlCentrePanel::QueryOpen() const noexcept
{
    return Notch.QueryOpen();
}

inline float ControlCentrePanel::QueryShadeProgress() const noexcept
{
    return Notch.QueryProgress();
}

inline ControlCentrePage ControlCentrePanel::QueryActivePage() const noexcept
{
    return ActivePage;
}

inline AppearanceSectionTab ControlCentrePanel::QueryAppearanceTab() const noexcept
{
    return ActiveAppearanceTab;
}

inline float ControlCentrePanel::QueryInterfaceScaleFactor() const noexcept
{
    return std::clamp(AppliedAppearance.InterfaceScale / 100.0f, 0.75f, 1.50f);
}

inline const ThemeCatalog& ControlCentrePanel::QueryThemeCatalog() const noexcept
{
    return AppliedTheme;
}

inline const AppearanceSettings& ControlCentrePanel::QueryAppliedAppearance() const noexcept
{
    return AppliedAppearance;
}

inline AppearanceSettings& ControlCentrePanel::AccessDraftAppearance() noexcept
{
    return DraftAppearance;
}

inline const InputPreferences& ControlCentrePanel::QueryAppliedInput() const noexcept
{
    return AppliedInput;
}

inline InputPreferences& ControlCentrePanel::AccessDraftInput() noexcept
{
    return DraftInput;
}

inline const NotificationPreferences& ControlCentrePanel::QueryAppliedNotifications() const noexcept
{
    return AppliedNotifications;
}

inline NotificationPreferences& ControlCentrePanel::AccessDraftNotifications() noexcept
{
    return DraftNotifications;
}

inline NotificationSequence& ControlCentrePanel::AccessNotificationStream() noexcept
{
    return Notifications;
}

inline ConfirmationDialogue& ControlCentrePanel::AccessConfirmationModal() noexcept
{
    return Dialogue;
}

inline bool ControlCentrePanel::QueryDetachedWindowsAllowed() const noexcept
{
    return DetachedWindowsAllowed;
}

inline void ControlCentrePanel::ConfigureScrollOffset(float Offset) noexcept
{
    ScrollOffset = std::max(0.0f, Offset);
}

} // namespace Frontier
