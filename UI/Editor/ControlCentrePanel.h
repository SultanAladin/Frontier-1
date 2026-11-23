/*==========================================================================================================================================
                                                            CONTROLCENTREPANEL.H
==========================================================================================================================================*/
// 🧩 Lifecycle-owned quick panel and native theme and typography controls.

#pragma once

#include "ContentBrowserPanel.h"
#include "Engine/ContentInterchange/ProjectSpaceCodec.h"
#include "Engine/Interface/AppearanceCodec.h"
#include "Engine/Interface/ConfigurationInspector.h"
#include "Engine/Interface/ConfirmationDialogue.h"
#include "Engine/Interface/InputPreferencesCodec.h"
#include "Engine/Interface/NotificationSequence.h"
#include "Engine/Interface/PanelExpansionTab.h"
#include "Engine/Interface/TypefaceRegistry.h"
#include "Engine/Lifecycle/ActivationSequence.h"
#include <array>
#include <filesystem>
#include <string>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTROL CENTRE
//------------------------------------------------------------------------------------------------------------------------

enum class AppearancePageCategory
{
    Display,
    Fonts,
    Theme
};

enum class ControlCentrePageCategory
{
    QuickControls,
    Settings,
    Appearance,
    Input,
    Notifications,
    Workspace
};

class ControlCentrePanel final : public ActivationSequence
{
  public:
    bool                                  ConfigurePreferenceScope(PreferenceScopeCategory Scope) noexcept;
    bool                                  ConfigurePersonalOverrides(bool Enabled) noexcept;
    bool                                  ReloadProfiles() noexcept;
    bool                                  FinalizeNotificationPreferences() noexcept;
    [[nodiscard]] PreferenceScopeCategory QueryPreferenceScope() const noexcept
    {
        return SaveScope;
    }
    [[nodiscard]] bool QueryPersonalOverrides() const noexcept
    {
        return PersonalOverrides;
    }
    [[nodiscard]] PlaneExtent QueryScopeExtent(size_t Index) const noexcept
    {
        return Index < ScopeExtents.size() ? ScopeExtents[Index] : PlaneExtent{};
    }
    explicit ControlCentrePanel(std::filesystem::path FontDirectory   = "EngineContent/FontArchives",
                                std::filesystem::path PreferencesPath = {}, std::filesystem::path ProjectPath = {});
    void                                    PrepareInterface() noexcept;
    void                                    ConstructInterface() noexcept;
    void                                    ActivatePage(AppearancePageCategory Page) noexcept;
    void                                    ActivateSettingsPage(ControlCentrePageCategory Page) noexcept;
    void                                    ActivatePreviousPage() noexcept;
    [[nodiscard]] ControlCentrePageCategory QueryPage() const noexcept
    {
        return ActivePage;
    }
    [[nodiscard]] AppearancePageCategory QueryAppearancePage() const noexcept
    {
        return SelectedPage;
    }
    [[nodiscard]] PlaneExtent QuerySettingsExtent() const noexcept
    {
        return SettingsExtent;
    }
    [[nodiscard]] PlaneExtent QueryBackExtent() const noexcept
    {
        return BackExtent;
    }
    [[nodiscard]] PlaneExtent QueryCategoryExtent(uint32_t Index) const noexcept
    {
        return Index < CategoryExtents.size() ? CategoryExtents[Index] : PlaneExtent{};
    }
    [[nodiscard]] PlaneExtent QueryQuickTileExtent(uint32_t Index) const noexcept
    {
        return Index < TileExtents.size() ? TileExtents[Index] : PlaneExtent{};
    }
    [[nodiscard]] PlaneExtent QueryTabExtent(uint32_t Index) const noexcept
    {
        return Index < TabExtents.size() ? TabExtents[Index] : PlaneExtent{};
    }
    [[nodiscard]] PlaneExtent QueryApplyExtent() const noexcept
    {
        return ApplyExtent;
    }
    [[nodiscard]] InputInspector &QueryInput() noexcept
    {
        return Input;
    }
    [[nodiscard]] NotificationInspector &QueryNotifications() noexcept
    {
        return Notifications;
    }
    void ConfigureContentBrowserVisibility(bool Visible) noexcept { BrowserVisible = Visible; }
    void ConfigureDockedPanels(bool ContentDocked, bool NotificationsDocked) noexcept
    {
        BrowserDocked = ContentDocked;
        StreamDocked  = NotificationsDocked;
    }
    void               ConstructHostedNotificationStream() noexcept;
    [[nodiscard]] bool QueryDetachedWindows() const noexcept
    {
        return DetachedWindows;
    }
    [[nodiscard]] bool QueryDetachedDraft() const noexcept
    {
        return DetachedDraft;
    }
    void ConfigureDetachedDraft(bool Enabled) noexcept
    {
        DetachedDraft = Enabled;
    }
    [[nodiscard]] bool QueryDockableContentBrowser() const noexcept
    {
        return DockableBrowser;
    }
    [[nodiscard]] bool QueryDockableBrowserDraft() const noexcept
    {
        return DockableBrowserDraft;
    }
    void ConfigureDockableBrowserDraft(bool Enabled) noexcept
    {
        DockableBrowserDraft = Enabled;
    }
    [[nodiscard]] PlaneExtent QueryBrowserHostingExtent() const noexcept
    {
        return BrowserHostingExtent;
    }
    [[nodiscard]] const std::filesystem::path &QueryPersonalProjectPath() const noexcept
    {
        return PreferencesPath;
    }
    bool PersistWorkspaceLayout(const toml::table &Layout, std::string &Message,
                                std::string_view LayoutKey = "layout") noexcept;
    void ConfigureLayoutDiagnostic(std::string Message)
    {
        LayoutDiagnostic = std::move(Message);
    }
    [[nodiscard]] uint64_t QueryLayoutResetRevision() const noexcept
    {
        return LayoutResetRevision;
    }
    void RequestLayoutReset() noexcept
    {
        ++LayoutResetRevision;
    }
    [[nodiscard]] PlaneExtent QueryLayoutResetExtent() const noexcept
    {
        return LayoutResetExtent;
    }
    [[nodiscard]] bool QueryWorkspaceDirty() const noexcept
    {
        return DetachedDraft != DetachedWindows || DockableBrowserDraft != DockableBrowser;
    }
    [[nodiscard]] bool        FinalizeWorkspacePreferences() noexcept;
    [[nodiscard]] PlaneExtent QueryDetachedToggleExtent() const noexcept
    {
        return DetachedToggleExtent;
    }
    [[nodiscard]] const NotificationSequence &QueryNotificationHistory() const noexcept
    {
        return Alerts;
    }
    [[nodiscard]] PlaneExtent QueryNotificationExtent(size_t Index) const noexcept
    {
        return Index < AlertVisibleCount ? QueryPhysicalExtent(AlertExtents[Index]) : PlaneExtent{};
    }
    [[nodiscard]] size_t QueryVisibleNotifications() const noexcept
    {
        return AlertVisibleCount;
    }
    [[nodiscard]] PlaneExtent QueryNotificationClearExtent() const noexcept
    {
        return HistoryClearExtent;
    }
    [[nodiscard]] PlaneExtent QueryNotificationStreamExtent() const noexcept
    {
        return StreamExtent;
    }
    [[nodiscard]] const ConfirmationDialogue &QueryConfirmation() const noexcept
    {
        return Confirmation;
    }
    [[nodiscard]] bool QueryNotificationsEnabled() const noexcept
    {
        return NotificationsEnabled;
    }
    [[nodiscard]] bool QueryPageMoving() const noexcept
    {
        return PageTime < PageSwapDuration * 2;
    }
    [[nodiscard]] bool                         FinalizePreferences() noexcept;
    [[nodiscard]] bool                         FinalizeInputPreferences() noexcept;
    [[nodiscard]] const std::filesystem::path &QueryInputPreferencesPath() const noexcept
    {
        return InputPreferencesPath;
    }
    [[nodiscard]] PlaneExtent QueryDefaultsExtent() const noexcept
    {
        return DefaultsExtent;
    }
    [[nodiscard]] PlaneExtent QueryFooterExtent() const noexcept
    {
        return FooterExtent;
    }
    void ResetPreferences() noexcept
    {
        Appearance.Discard();
    }
    [[nodiscard]] AppearanceInspector &QueryAppearance() noexcept
    {
        return Appearance;
    }
    [[nodiscard]] const TypefaceRegistry &QueryTypefaces() const noexcept
    {
        return Typefaces;
    }
    [[nodiscard]] const std::string &QueryDiagnostic() const noexcept
    {
        return Diagnostic;
    }
    [[nodiscard]] bool QueryCentreOpen() const noexcept
    {
        return ActivePage != ControlCentrePageCategory::QuickControls && Expansion.QueryOpen();
    }
    [[nodiscard]] bool QueryQuickOpen() const noexcept
    {
        return Expansion.QueryOpen();
    }

    [[nodiscard]] ContentBrowserPanel &QueryContentBrowser() noexcept
    {
        return Browser;
    }

    [[nodiscard]] const PanelExpansionTab &QueryExpansionTab() const noexcept
    {
        return Expansion;
    }

  protected:
    bool InitializeContent() noexcept override;
    void IntegrateContent(double Δτ) noexcept override;
    void FinalizeContent() noexcept override;

  private:
    bool  PersistWorkspaceSection(ControlCentrePageCategory Page) noexcept;
    void  ConstructDrawerLayout() noexcept;
    void  ConstructQuickLayout(PixelSpace &Surface, const PlaneExtent &Card, const ControlPointer &Pointer) noexcept;
    void  ConstructCategoryLayout(PixelSpace &Surface, const PlaneExtent &Card, const ControlPointer &Pointer) noexcept;
    void  ConstructPreferenceLayout(PixelSpace &Surface, const PlaneExtent &Card,
                                    const ControlPointer &Pointer) noexcept;
    void  ConstructScrollableLayout(const PlaneExtent &Body, const ControlPointer &Pointer) noexcept;
    void  BroadcastNotice(bool Failed, bool AppliedOnly = false) noexcept;
    float ConstructWorkspaceSettings(PixelSpace &Surface, const PlaneExtent &Column,
                                     const ControlPointer &Pointer) noexcept;
    void  ConstructNotificationStack(bool Paint) noexcept;
    void  ConstructNotificationStream(const PlaneExtent &Body, const ControlPointer &Pointer) noexcept;
    void  ActivatePreferenceConfirmation() noexcept;
    float ConstructNotificationHistory(PixelSpace &Surface, const PlaneExtent &Column, float Offset,
                                       const ControlPointer &Pointer) noexcept;
    void  IntegrateKeyboard() noexcept;
    [[nodiscard]] PlaneExtent QueryPhysicalExtent(const PlaneExtent &Extent) const noexcept;
    void                      ConfigureCardExtent(float Width, float Height) noexcept;

    std::filesystem::path      ProjectPath;
    ProjectSpaceDocument       EngineDefaults;
    std::string                RequestedFamily   = "Inter";
    uint32_t                   FallbackFamily    = 0;
    bool                       FamilyUnavailable = false;
    bool                       ProjectWritable   = false;
    bool                       PersonalOverrides = false;
    PreferenceScopeCategory    SaveScope         = PreferenceScopeCategory::Personal;
    std::array<PlaneExtent, 2> ScopeExtents{};
    std::filesystem::path      FontDirectory;
    std::filesystem::path      PreferencesPath;
    std::filesystem::path      InputPreferencesPath;
    TypefaceRegistry           Typefaces;
    AppearanceInspector        Appearance;
    ThemeStructure             Theme;
    std::string                Diagnostic;
    std::string                AppearanceDiagnostic;
    std::string                InputDiagnostic;
    AppearancePageCategory     SelectedPage    = AppearancePageCategory::Fonts;
    uint32_t                   AppliedRevision = ~0u;
    ControlCentrePageCategory  ActivePage      = ControlCentrePageCategory::QuickControls;
    ControlCentrePageCategory  DisplayedPage   = ControlCentrePageCategory::QuickControls;
    ControlCentrePageCategory  DepartingPage   = ControlCentrePageCategory::QuickControls;
    InputInspector             Input;
    NotificationInspector      Notifications;
    MotionIntegrator           CardMotion;
    uint32_t                   CardWidth;
    uint32_t                   CardHeight;
    static constexpr float     PageSwapDuration     = 0.20f;
    float                      PageTime             = PageSwapDuration * 2;
    float                      PageAlpha            = 1;
    bool                       Forward              = true;
    bool                       ResetScroll          = false;
    bool                       DetachedWindows      = false;
    bool                       DetachedDraft        = false;
    bool                       DockableBrowser      = false;
    bool                       DockableBrowserDraft = false;
    PlaneExtent                BrowserHostingExtent;
    bool                       BrowserVisible = true;
    bool                       BrowserDocked = false;
    bool                       StreamDocked  = false;
    PlaneExtent                DetachedToggleExtent;
    PlaneExtent                LayoutResetExtent;
    std::string                LayoutDiagnostic;
    uint64_t                   LayoutResetRevision = 0;
    ConfirmationDialogue       Confirmation;
    ControlCentrePageCategory  ConfirmationPage  = ControlCentrePageCategory::QuickControls;
    PreferenceScopeCategory    ConfirmationScope = PreferenceScopeCategory::Personal;
    PlaneExtent                StreamExtent;
    NotificationSequence       Alerts;
    PlaneExtent                HistoryClearExtent;
    std::array<PlaneExtent, NotificationSequence::VisibleCapacity> AlertExtents{};
    size_t                                                         AlertVisibleCount = 0;
    uint64_t                                                       AlertHoverToken   = 0;
    uint64_t                                                       AlertPressToken   = 0;
    uint64_t                                                       AlertDismissToken = 0;
    bool                                                           AlertContact      = false;
    PlaneExtent                SettingsExtent, BackExtent, ApplyExtent, DefaultsExtent, FooterExtent;
    std::array<PlaneExtent, 4> CategoryExtents{};
    std::array<PlaneExtent, 2> TileExtents{};
    std::array<PlaneExtent, 3> TabExtents{};
    PlaneExtent                CurrentCard;
    bool                       ControlContact = false;
    PanelExpansionTab          Expansion;
    ContentBrowserPanel        Browser;
    bool                       NotificationsEnabled = true;
};
} // namespace Frontier
