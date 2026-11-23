/*==========================================================================================================================================
                                                           CONTROLCENTREPANEL.CPP
==========================================================================================================================================*/
// 🧩 Lifecycle-owned quick panel and native theme and typography controls.

#include "ControlCentrePanel.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include "Engine/Interface/IconPresentation.h"
#include "Engine/Interface/PanelDockspace.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <utility>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                      LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

ControlCentrePanel::ControlCentrePanel(std::filesystem::path Directory, std::filesystem::path Preferences,
                                       std::filesystem::path Project)
    : ProjectPath(std::move(Project)), FontDirectory(std::move(Directory)), PreferencesPath(std::move(Preferences)),
      CardWidth(CardMotion.Register(420)), CardHeight(CardMotion.Register(480)), Browser(FontDirectory.parent_path())
{
}

bool ControlCentrePanel::InitializeContent() noexcept
{
    try
    {
        if (ImGui::GetCurrentContext() == nullptr)
            return false;
        if (Typefaces.Load(FontDirectory) == 0)
        {
            Diagnostic = "No usable packaged fonts; using the built-in emergency face.";
            DiagnosticJournal::Append("FontFallback", Diagnostic);
            if (ImGui::GetIO().Fonts->Fonts.empty())
                ImGui::GetIO().Fonts->AddFontDefault();
        }
        TypefaceRegistry::Install(&Typefaces);
        if (PreferencesPath.extension() == ".projectspace" || !ProjectPath.empty())
        {
            if (!ReloadProfiles())
                return false;
            return Browser.Initialize() && Browser.Activate();
        }
        AppearanceSettings Preferences;
        if (!PreferencesPath.empty() && std::filesystem::exists(PreferencesPath))
        {
            std::string FamilyName;
            if (AppearanceCodec::Decode(PreferencesPath, Preferences, FamilyName, Diagnostic))
            {
                const auto FamilyIndex = Typefaces.ResolveFamilyIndex(FamilyName);
                Preferences.FontFamily = FamilyIndex >= 0 ? static_cast<uint32_t>(FamilyIndex) : 0;
                if (FamilyIndex < 0)
                    Diagnostic = "Saved font unavailable; using the first installed family.";
            }
        }
        Appearance.Seed(Preferences);
        AppearanceDiagnostic = Diagnostic;
        InputPreferences KeyboardPreferences;
        if (!PreferencesPath.empty())
        {
            auto InputFileName = std::filesystem::path("Input.");
            InputFileName += PreferencesPath.filename().native();
            InputPreferencesPath = PreferencesPath.parent_path() / InputFileName;
            if (std::filesystem::exists(InputPreferencesPath))
            {
                (void)InputPreferencesCodec::Decode(InputPreferencesPath, KeyboardPreferences, Diagnostic);
                InputDiagnostic = Diagnostic;
            }
        }
        Input.InitializePreferences(KeyboardPreferences);
        return Browser.Initialize() && Browser.Activate();
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

void ControlCentrePanel::IntegrateContent(double Δτ) noexcept
{
    Expansion.Integrate(Δτ);
    (void)Browser.Integrate(Δτ);
    (void)CardMotion.Integrate(Δτ);
    PageTime = std::min(PageTime + static_cast<float>(std::max(0.0, Δτ)), PageSwapDuration * 2);
    Alerts.Integrate(Δτ, AlertVisibleCount, AlertHoverToken ? AlertHoverToken : AlertPressToken);
    Appearance.IntegrateFontStrip(static_cast<float>(std::min(Δτ, 0.1)));
}

void ControlCentrePanel::FinalizeContent() noexcept
{
    (void)Browser.Finalize();
    Alerts.ResetPresentation();
    Alerts.ResetHistory();
    Confirmation.Reset();
    AlertVisibleCount = 0;
    AlertPressToken = AlertDismissToken = AlertHoverToken = 0;
    AlertContact                                          = false;
    if (TypefaceRegistry::QueryCurrent() == &Typefaces)
        TypefaceRegistry::Install(nullptr);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                APPLIED PREFERENCES
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::PrepareInterface() noexcept
{
    if (QueryPhase() != ActivationPhase::Active || AppliedRevision == Appearance.QueryRevision())
        return;
    const auto &Preferences = Appearance.QueryApplied();
    Theme.ConfigureTheme(Preferences.Theme);
    Theme.ConfigureAccent(Preferences.Accent);
    Theme.ConfigureCornerRadius(Preferences.CornerRadius);
    ControlKit::ConfigureTheme(Theme, AppearanceInspector::QuerySemanticColour(0, Preferences.WarningSwatch),
                               AppearanceInspector::QuerySemanticColour(1, Preferences.SuccessSwatch),
                               AppearanceInspector::QuerySemanticColour(2, Preferences.InfoSwatch),
                               AppearanceInspector::QuerySemanticColour(3, Preferences.CautionSwatch));
    if (ControlKit::Palette().LightSurface)
        ImGui::StyleColorsLight();
    else
        ImGui::StyleColorsDark();
    auto       &Style   = ImGui::GetStyle();
    const auto &Palette = Theme.QueryPalette();
    const auto  Colour  = [](ColorQuad Tone) { return ImVec4(Tone.Red, Tone.Green, Tone.Blue, Tone.Alpha); };
    Style.Colors[ImGuiCol_WindowBg]      = Colour(Palette.MainBackground);
    Style.Colors[ImGuiCol_ChildBg]       = Colour(Palette.MainBackground);
    Style.Colors[ImGuiCol_Text]          = Colour(Palette.TextMain);
    Style.Colors[ImGuiCol_TextDisabled]  = Colour(Palette.TextMuted);
    Style.Colors[ImGuiCol_Button]        = Colour(Palette.ActiveBackground);
    Style.Colors[ImGuiCol_ButtonHovered] = Colour(Palette.CardSubBackground);
    Style.Colors[ImGuiCol_ButtonActive]  = Colour(Theme.QueryAccentColor());
    Style.Colors[ImGuiCol_CheckMark]     = Colour(Theme.QueryAccentColor());
    Style.Colors[ImGuiCol_SliderGrab]    = Colour(Theme.QueryAccentColor());
    // 📝 Native patched tabs share the applied surface, including unfocused and light-theme variants.
    Style.Colors[ImGuiCol_TitleBg]                   = Colour(Palette.PanelBackground);
    Style.Colors[ImGuiCol_TitleBgActive]             = Colour(Palette.PanelBackground);
    Style.Colors[ImGuiCol_DockingEmptyBg]            = Colour(Palette.MainBackground);
    const auto TabTone                               = Colour(Theme.QueryDarkestSurface());
    Style.Colors[ImGuiCol_Tab]                       = TabTone;
    Style.Colors[ImGuiCol_TabHovered]                = TabTone;
    Style.Colors[ImGuiCol_TabSelected]               = TabTone;
    Style.Colors[ImGuiCol_TabDimmed]                 = TabTone;
    Style.Colors[ImGuiCol_TabDimmedSelected]         = TabTone;
    Style.Colors[ImGuiCol_TabSelectedOverline]       = ImVec4(0, 0, 0, 0);
    Style.Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    Style.Colors[ImGuiCol_DockingPreview] =
        ImVec4(Style.Colors[ImGuiCol_CheckMark].x, Style.Colors[ImGuiCol_CheckMark].y,
               Style.Colors[ImGuiCol_CheckMark].z, .12f);
    Style.WindowRounding       = Preferences.CornerRadius;
    Style.ChildRounding        = Preferences.CornerRadius;
    Style.FrameRounding        = Preferences.CornerRadius * 0.5f;
    Style.FontSizeBase         = Preferences.RoleSize[3];
    Style.FontScaleMain        = Preferences.InterfaceScale / 100.0f;
    ImGui::GetIO().FontDefault = static_cast<ImFont *>(Appearance.QueryAppliedFace(3));
    AppliedRevision            = Appearance.QueryRevision();
}

bool ControlCentrePanel::FinalizePreferences() noexcept
{
    const auto &Preferences = Appearance.QueryDraft();
    const auto *Family      = Typefaces.QueryFamily(Preferences.FontFamily);
    if ((!Family && Typefaces.QueryFamilyCount() != 0) || !AppearanceCodec::Validate(Preferences))
    {
        Diagnostic           = "Invalid appearance preferences.";
        AppearanceDiagnostic = Diagnostic;
        BroadcastNotice(true);
        return false;
    }
    if ((SaveScope == PreferenceScopeCategory::Project || PreferencesPath.extension() == ".projectspace") &&
        !PersistWorkspaceSection(ControlCentrePageCategory::Appearance))
    {
        AppearanceDiagnostic = Diagnostic;
        BroadcastNotice(true);
        return false;
    }
    if (SaveScope == PreferenceScopeCategory::Personal && PreferencesPath.extension() != ".projectspace" &&
        !PreferencesPath.empty() &&
        !AppearanceCodec::Encode(PreferencesPath, Preferences, Family ? Family->Name : RequestedFamily, Diagnostic))
    {
        AppearanceDiagnostic = Diagnostic;
        BroadcastNotice(true);
        return false;
    }
    Appearance.Apply();
    if (SaveScope != PreferenceScopeCategory::Project && PreferencesPath.extension() != ".projectspace")
        Diagnostic = PreferencesPath.empty() ? "Applied for this session." : "Preferences saved.";
    AppearanceDiagnostic = Diagnostic;
    BroadcastNotice(false);
    return true;
}

bool ControlCentrePanel::FinalizeInputPreferences() noexcept
{
    if (!InputChordTable::Validate(Input.QueryDraft(), Diagnostic))
    {
        InputDiagnostic = Diagnostic;
        BroadcastNotice(true);
        return false;
    }
    if ((SaveScope == PreferenceScopeCategory::Project || PreferencesPath.extension() == ".projectspace") &&
        !PersistWorkspaceSection(ControlCentrePageCategory::Input))
    {
        InputDiagnostic = Diagnostic;
        BroadcastNotice(true);
        return false;
    }
    if (SaveScope == PreferenceScopeCategory::Personal && PreferencesPath.extension() != ".projectspace" &&
        !InputPreferencesPath.empty() &&
        !InputPreferencesCodec::Encode(InputPreferencesPath, Input.QueryDraft(), Diagnostic))
    {
        InputDiagnostic = Diagnostic;
        BroadcastNotice(true);
        return false;
    }
    Input.FinalizePreferences();
    if (SaveScope != PreferenceScopeCategory::Project && PreferencesPath.extension() != ".projectspace")
        Diagnostic = InputPreferencesPath.empty() ? "Input applied for this session." : "Input preferences saved.";
    InputDiagnostic = Diagnostic;
    BroadcastNotice(false);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              REFERENCE NAVIGATION
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::ActivateSettingsPage(ControlCentrePageCategory Page) noexcept
{
    if (Page != ActivePage)
    {
        DepartingPage = DisplayedPage;
        Forward =
            Page != ControlCentrePageCategory::QuickControls &&
            !(Page == ControlCentrePageCategory::Settings && ActivePage != ControlCentrePageCategory::QuickControls);
        ActivePage = Page;
        PageTime   = 0;
        Appearance.CloseMenus();
        Input.ResetInteraction();
        ResetScroll = true;
    }
    Expansion.ConfigureOpen(true);
}

void ControlCentrePanel::ActivatePage(AppearancePageCategory Page) noexcept
{
    SelectedPage = Page;
    ActivateSettingsPage(ControlCentrePageCategory::Appearance);
    ResetScroll = true;
}

void ControlCentrePanel::ActivatePreviousPage() noexcept
{
    if (ActivePage == ControlCentrePageCategory::QuickControls)
        Expansion.ConfigureOpen(false);
    else
        ActivateSettingsPage(ActivePage == ControlCentrePageCategory::Settings
                                 ? ControlCentrePageCategory::QuickControls
                                 : ControlCentrePageCategory::Settings);
    // Back preserves drafts. Only the explicit Discard action throws them away.
}

void ControlCentrePanel::ConfigureCardExtent(float Width, float Height) noexcept
{
    if (CardMotion.Spring(CardWidth).Target != Width)
        CardMotion.Spring(CardWidth).Depart(Width);
    if (CardMotion.Spring(CardHeight).Target != Height)
        CardMotion.Spring(CardHeight).Depart(Height);
}

PlaneExtent ControlCentrePanel::QueryPhysicalExtent(const PlaneExtent &Extent) const noexcept
{
    const float Scale = Appearance.QueryApplied().InterfaceScale / 100.0f;
    return {Extent.MinimumX * Scale, Extent.MinimumY * Scale, Extent.MaximumX * Scale, Extent.MaximumY * Scale};
}

void ControlCentrePanel::BroadcastNotice(bool Failed, bool AppliedOnly) noexcept
{
    const auto &Preferences = Notifications.QueryApplied();
    const bool  Presentation =
        NotificationsEnabled && (Failed ? Preferences.PreferenceErrors : Preferences.PreferencesApplied);
    const bool SessionOnly = AppliedOnly || (SaveScope == PreferenceScopeCategory::Personal && PreferencesPath.empty());
    Alerts.Broadcast(Failed ? NotificationCategory::Error : NotificationCategory::Success,
                     Failed        ? "Preferences could not be saved"
                     : SessionOnly ? "Preferences applied"
                                   : "Preferences saved",
                     Diagnostic, Preferences.HoldSeconds, Presentation);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             QUICK CONTROLS AND SETTINGS HUB
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::ConstructQuickLayout(PixelSpace &Surface, const PlaneExtent &Card,
                                              const ControlPointer &Pointer) noexcept
{
    const auto &K = ControlKit::Palette();
    ControlKit::TextLeading(Surface, Spanning(Card.MinimumX, Card.MinimumY, Card.Width(), 16), 8, K.TextDim,
                            "Quick Controls", 13);
    SettingsExtent      = QueryPhysicalExtent(Spanning(Card.MaximumX - 24, Card.MinimumY, 16, 16));
    const auto Settings = Spanning(Card.MaximumX - 24, Card.MinimumY, 16, 16);
    ControlKit::GlyphCentred(Surface, Settings, 16, K.Text, ControlCentreIconCategory::SettingsGear);
    const bool OverSettings = ControlKit::Over(Settings, Pointer);
    ControlContact          = ControlContact || OverSettings;
    if (OverSettings && Pointer.Released)
        ActivateSettingsPage(ControlCentrePageCategory::Settings);

    // Preserve the inherited 64 px circles / 24 px SVGs / 12 px labels and
    // four-column grid. Only the two surviving non-renderer toggles are packed
    // into that grid; no font/theme shortcuts.
    const bool                      Split     = Card.Width() >= 760;
    const float                     TileWidth = Split ? 256.0f : Card.Width();
    const uint32_t                  Columns   = std::clamp(static_cast<uint32_t>((TileWidth + 16) / 80), 1u, 2u);
    const float                     Cell      = (TileWidth - 16 * (Columns - 1)) / Columns;
    const char                     *Labels[]  = {"FPS Overlay", "Notifications"};
    const ControlCentreIconCategory Icons[]   = {ControlCentreIconCategory::GaugeFrameRate,
                                                 ControlCentreIconCategory::NotificationsBell};
    for (uint32_t Index = 0; Index < 2; ++Index)
    {
        const float X      = Card.MinimumX + (Index % Columns) * (Cell + 16) + (Cell - 64) * 0.5f;
        const float Y      = Card.MinimumY + 56 + (Index / Columns) * 130;
        const auto  Disc   = Spanning(X, Y, 64, 64);
        TileExtents[Index] = QueryPhysicalExtent(Disc);
        const bool On      = Index == 0 ? Notifications.QueryApplied().ShowFrameRateOverlay : NotificationsEnabled;
        const bool Hover   = ControlKit::Over(Disc, Pointer);
        ControlContact     = ControlContact || Hover;
        Surface.FillRectangle(Disc, On ? K.Accent : K.Raised, 32);
        ControlKit::GlyphCentred(Surface, Disc, 24, On ? K.AccentInk : K.TextDim, Icons[Index]);
        if (Hover)
            ControlKit::OutlineRounded(Surface, Disc, K.Text, 32);
        ControlKit::TextCentred(Surface, Spanning(X - (Cell - 64) * 0.5f, Y + 76, Cell, 14), On ? K.Text : K.TextDim,
                                Labels[Index], 12);
        if (Hover && Pointer.Released)
        {
            if (Index == 0)
                Notifications.ConfigureFrameRateOverlay(!On);
            else
            {
                NotificationsEnabled = !On;
                if (!NotificationsEnabled)
                    Alerts.ResetPresentation();
            }
        }
    }
    const float StreamX = Split ? Card.MinimumX + TileWidth + 40 : Card.MinimumX;
    const float StreamY = Card.MinimumY + (Split ? 48 : 210);
    if (Split)
        Surface.FillRectangle(Spanning(StreamX - 20, StreamY, 1, Card.MaximumY - StreamY), K.Divider);
    if (!StreamDocked)
        ConstructNotificationStream(Spanning(StreamX, StreamY, Card.MaximumX - StreamX, Card.MaximumY - StreamY),
                                    Pointer);
    else
        Surface.Text(StreamX, StreamY + 10, K.TextDim, "Notification Stream is open in a workspace tab.", 13);
}

void ControlCentrePanel::ConstructCategoryLayout(PixelSpace &Surface, const PlaneExtent &Card,
                                                 const ControlPointer &Pointer) noexcept
{
    const auto &K    = ControlKit::Palette();
    const auto  Back = Spanning(Card.MinimumX + 8, Card.MinimumY + 8, 36, 24);
    BackExtent       = QueryPhysicalExtent(Back);
    ControlKit::GlyphCentred(Surface, Back, 24, K.TextDim, ControlCentreIconCategory::ChevronBack);
    ControlKit::TextLeading(Surface, Spanning(Card.MinimumX + 44, Card.MinimumY, Card.Width() - 44, 41), 0, K.Text,
                            "Settings", 22);
    ControlContact = ControlContact || ControlKit::Over(Back, Pointer);
    if (ControlKit::Over(Back, Pointer) && Pointer.Released)
        ActivatePreviousPage();
    const char *Names[]        = {"Appearance", "Input", "Notifications", "Workspace"};
    const char *Descriptions[] = {"Typography, theme, and interface scale", "Shortcuts and pointer response",
                                  "FPS overlay and interface alerts", "Docked tabs and optional OS windows"};
    const ControlCentreIconCategory Icons[] = {
        ControlCentreIconCategory::AppearancePalette, ControlCentreIconCategory::ShieldInput,
        ControlCentreIconCategory::NotificationsBell, ControlCentreIconCategory::DisplayMonitor};
    const ControlCentrePageCategory Pages[] = {ControlCentrePageCategory::Appearance, ControlCentrePageCategory::Input,
                                               ControlCentrePageCategory::Notifications,
                                               ControlCentrePageCategory::Workspace};
    const auto                      List    = Spanning(Card.MinimumX, Card.MinimumY + 89, Card.Width(), 308);
    Surface.FillRectangle(List, K.Raised, 24);
    ControlKit::OutlineRounded(Surface, List, K.Stroke, 24);
    for (uint32_t Index = 0; Index < 4; ++Index)
    {
        const auto Row         = Spanning(List.MinimumX, List.MinimumY + Index * 77, List.Width(), 76);
        CategoryExtents[Index] = QueryPhysicalExtent(Row);
        const bool Hover       = ControlKit::Over(Row, Pointer);
        ControlContact         = ControlContact || Hover;
        if (Hover)
            Surface.FillRectangle(Row, K.Selected, (Index == 0 || Index == 3) ? 24 : 0);
        const auto Disc = Spanning(Row.MinimumX + 16, Row.MinimumY + 16, 44, 44);
        Surface.FillRectangle(Disc, K.Selected, 22);
        ControlKit::GlyphCentred(Surface, Disc, 20, K.TextDim, Icons[Index]);
        Surface.Text(Row.MinimumX + 76, Row.MinimumY + 17, K.Text, Names[Index], 15);
        Surface.Text(Row.MinimumX + 76, Row.MinimumY + 39, K.TextDim, Descriptions[Index], 13);
        ControlKit::GlyphCentred(Surface, Spanning(Row.MaximumX - 34, Row.MinimumY + 29, 18, 18), 18, K.TextDim,
                                 ControlCentreIconCategory::ChevronForward);
        if (Index < 3)
            Surface.FillRectangle(Spanning(Row.MinimumX, Row.MaximumY, Row.Width(), 1), K.Divider);
        if (Hover && Pointer.Released)
            ActivateSettingsPage(Pages[Index]);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SETTINGS SUB-PAGES
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::ConstructScrollableLayout(const PlaneExtent &Body, const ControlPointer &Pointer) noexcept
{
    const float Scale = Appearance.QueryApplied().InterfaceScale / 100.0f;
    ImGui::SetCursorScreenPos(ImVec2(Body.MinimumX * Scale, Body.MinimumY * Scale));
    ImGui::PushID(static_cast<int>(DisplayedPage));
    ImGui::PushID(DisplayedPage == ControlCentrePageCategory::Appearance ? static_cast<int>(SelectedPage) : 0);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, PageAlpha);
    const bool InputPage = DisplayedPage == ControlCentrePageCategory::Input;
    ImGui::BeginChild(DisplayedPage == ControlCentrePageCategory::Appearance ? "Appearance content"
                                                                             : "Settings content",
                      ImVec2(Body.Width() * Scale, std::max(1.0f, Body.Height() * Scale)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    if (ResetScroll && DisplayedPage == ActivePage)
    {
        ImGui::SetScrollY(0);
        ResetScroll = false;
    }
    const auto Start  = ImGui::GetCursorScreenPos();
    const auto Column = Spanning(Start.x / Scale, Start.y / Scale,
                                 std::max(1.0f, (ImGui::GetContentRegionAvail().x - 12) / Scale), 10000);
    PixelSpace Content;
    if (Content.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, Scale))
    {
        auto Local          = Pointer;
        Local.Enabled       = Local.Enabled && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const float Opacity = PageAlpha;
        float       Height  = 0;
        if (DisplayedPage == ControlCentrePageCategory::Appearance)
            Height = SelectedPage == AppearancePageCategory::Display
                         ? Appearance.ConstructDisplayTabLayout(Content, Column, 0, Local, Opacity)
                     : SelectedPage == AppearancePageCategory::Fonts
                         ? Appearance.ConstructFontsTabLayout(Content, Column, 0, Local, Opacity)
                         : Appearance.ConstructThemeTabLayout(Content, Column, 0, Local, Opacity);
        else if (InputPage)
            Height = Input.ConstructInputLayout(Content, Column, 0, Local, Opacity);
        else if (DisplayedPage == ControlCentrePageCategory::Workspace)
            Height = ConstructWorkspaceSettings(Content, Column, Local);
        else
        {
            Height = Notifications.ConstructNotificationLayout(Content, Column, 0, Local, Opacity);
        }
        ImGui::Dummy(ImVec2(Column.Width() * Scale, Height * Scale));
        // The scrollbar is a genuine ImGui item; custom controls reserve pointer
        // ownership through the body.
        ControlContact = ControlContact || Body.Encloses(Pointer.X, Pointer.Y);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopID();
    ImGui::PopID();
}

void ControlCentrePanel::ConstructPreferenceLayout(PixelSpace &Surface, const PlaneExtent &Card,
                                                   const ControlPointer &Pointer) noexcept
{
    const auto &K                  = ControlKit::Palette();
    const bool  AppearancePageOpen = DisplayedPage == ControlCentrePageCategory::Appearance;
    const bool  InputPage          = DisplayedPage == ControlCentrePageCategory::Input;
    const bool  WorkspacePage      = DisplayedPage == ControlCentrePageCategory::Workspace;
    const char *Title              = AppearancePageOpen ? "Appearance"
                                     : InputPage        ? "Input Settings"
                                     : WorkspacePage    ? "Workspace"
                                                        : "Telemetry & Notifications";
    const char *Subtitle           = AppearancePageOpen ? "Customize your workspace visual experience"
                                     : InputPage        ? "Configure keyboard shortcuts and pointer response"
                                     : WorkspacePage    ? "Panel hosting - personal preference"
                                                        : "Overlay and alerts - explicit save scope";
    Surface.FillRectangle(Card, K.Panel, 32);
    ControlKit::OutlineRounded(Surface, Card, K.Stroke, 32);
    const float Pad = std::min(32.0f, Card.Width() * 0.05f);
    float       Y   = Card.MinimumY + 32;
    Surface.PushClip(Spanning(Card.MinimumX + Pad, Y, std::max(1.0f, Card.Width() - Pad * 2 - 40), 36));
    Surface.Text(Card.MinimumX + Pad, Y, K.Text, Title, Card.Width() < 500 ? 18 : 24);
    Surface.PopClip();
    Y += 44;
    Surface.PushClip(Spanning(Card.MinimumX + Pad, Y, std::max(1.0f, Card.Width() - Pad * 2), 24));
    Surface.Text(Card.MinimumX + Pad, Y, K.TextDim,
                 Card.Width() < 500 ? (InputPage            ? "Keyboard / pointer preferences"
                                       : AppearancePageOpen ? "Typography and theme"
                                       : WorkspacePage      ? "Optional detached windows"
                                                            : "Session overlay / alerts")
                                    : Subtitle,
                 14);
    Surface.PopClip();
    Y += 38;
    if (!WorkspacePage && (PreferencesPath.extension() == ".projectspace" || !ProjectPath.empty()))
    {
        const float ScopeWidth   = std::min(112.0f, (Card.Width() - Pad * 2 - 8) * 0.5f);
        auto        ScopePointer = Pointer;
        for (size_t Index = 0; Index < 2; ++Index)
        {
            const auto Extent =
                Spanning(Card.MinimumX + Pad + static_cast<float>(Index) * (ScopeWidth + 8), Y, ScopeWidth, 28);
            ScopeExtents[Index]  = QueryPhysicalExtent(Extent);
            ScopePointer.Enabled = Pointer.Enabled && (Index == 0 ? !PreferencesPath.empty() : ProjectWritable);
            ControlContact       = ControlContact || ControlKit::Over(Extent, Pointer);
            if (ControlKit::ChipButton(Surface, Extent, Index == 0 ? "Personal" : "Project",
                                       SaveScope == static_cast<PreferenceScopeCategory>(Index), ScopePointer)
                    .Clicked)
                (void)ConfigurePreferenceScope(static_cast<PreferenceScopeCategory>(Index));
        }
        Y += 34;
        if (ProjectWritable)
        {
            const auto      Extent = Spanning(Card.MinimumX + Pad, Y, Card.Width() - Pad * 2, 28);
            ButtonStructure OverrideButton{PersonalOverrides ? "Personal overrides: On" : "Personal overrides: Off",
                                           ButtonToneCategory::Ghost};
            OverrideButton.Height = 28;
            OverrideButton.Disabled =
                Appearance.QueryDirty() || Input.QueryDirty() || Notifications.QueryDirty() || QueryWorkspaceDirty();
            ControlContact = ControlContact || ControlKit::Over(Extent, Pointer);
            if (ControlKit::PillButton(Surface, Extent, OverrideButton, Pointer).Clicked)
                (void)ConfigurePersonalOverrides(!PersonalOverrides);
            Y += 34;
        }
    }
    const auto Back = Spanning(Card.MaximumX - Pad - 32, Card.MinimumY + 32, 32, 32);
    BackExtent      = QueryPhysicalExtent(Back);
    ControlKit::OutlineRounded(Surface, Back, K.StrokeStrong, 16);
    ControlKit::GlyphCentred(Surface, Back, 16, K.TextDim, ControlCentreIconCategory::ChevronBack);
    ControlContact = ControlContact || ControlKit::Over(Back, Pointer);
    if (ControlKit::Over(Back, Pointer) && Pointer.Released)
        ActivatePreviousPage();
    if (AppearancePageOpen)
    {
        const char *Tabs[] = {"Display", "Typography", "Theme"};
        float       X      = Card.MinimumX + Pad;
        Surface.FillRectangle(Spanning(X, Y + 37, Card.Width() - Pad * 2, 1), K.Divider);
        for (uint32_t Index = 0; Index < 3; ++Index)
        {
            const float Width = Surface.MeasureText(Tabs[Index], 14).X;
            const auto  Tab   = Spanning(X, Y, Width, 38);
            TabExtents[Index] = QueryPhysicalExtent(Tab);
            ControlContact    = ControlContact || ControlKit::Over(Tab, Pointer);
            const bool Active = static_cast<uint32_t>(SelectedPage) == Index;
            Surface.Text(X, Y, Active ? K.Text : K.TextDim, Tabs[Index], 14);
            if (Active)
                Surface.FillRectangle(Spanning(X, Y + 36, Width, 2), K.Text);
            if (ControlKit::Over(Tab, Pointer) && Pointer.Released && !Active)
            {
                SelectedPage = static_cast<AppearancePageCategory>(Index);
                Appearance.CloseMenus();
                ResetScroll = true;
            }
            X += Width + 24;
        }
        Y += 38;
    }
    const bool  StackedFooter = InputPage && Card.Width() <= 500;
    const float FooterHeight  = StackedFooter ? 156 : 88;
    const float ButtonTop     = Card.MaximumY - FooterHeight + (StackedFooter ? 104 : 34);
    const auto  Footer        = Spanning(Card.MinimumX, Card.MaximumY - FooterHeight, Card.Width(), FooterHeight);
    FooterExtent              = QueryPhysicalExtent(Footer);
    Surface.FillRectangleBottomRounded(Footer, K.Card, 32);
    Surface.FillRectangle(Spanning(Footer.MinimumX, Footer.MinimumY, Footer.Width(), 1), K.Divider);
    const bool  Dirty  = WorkspacePage        ? QueryWorkspaceDirty()
                         : AppearancePageOpen ? Appearance.QueryDirty()
                         : InputPage          ? Input.QueryDirty()
                                              : Notifications.QueryDirty();
    const char *Status = WorkspacePage
                             ? (Dirty ? "Window-hosting changes pending" : "Personal window-hosting preferences")
                         : InputPage ? (Input.QueryKeyCapture() ? "Press a keyboard chord; Escape cancels capture"
                                        : Dirty                 ? "Unsaved input changes"
                                                                : "No unsaved input changes")
                         : AppearancePageOpen ? (Dirty ? "Unsaved appearance changes" : "No unsaved changes")
                                              : (Dirty ? "Notification changes pending" : "Notification preferences");
    Surface.Text(Footer.MinimumX + Pad, Footer.MinimumY + 10, K.TextDim, Status, 10);
    const float ButtonWidth  = std::max(1.0f, std::min(132.0f, (Footer.Width() - Pad * 2 - 12) * 0.5f));
    const auto  Apply        = Spanning(Footer.MaximumX - Pad - ButtonWidth, ButtonTop, ButtonWidth, 36);
    const float DiscardWidth = std::min(112.0f, ButtonWidth);
    const auto  Discard      = Spanning(Apply.MinimumX - 12 - DiscardWidth, Apply.MinimumY, DiscardWidth, 36);
    ApplyExtent              = QueryPhysicalExtent(Apply);
    ControlContact           = ControlContact || ControlKit::Over(Apply, Pointer) || ControlKit::Over(Discard, Pointer);
    ButtonStructure ApplyButton{WorkspacePage ? "Apply"
                                : (PreferencesPath.extension() == ".projectspace" || !ProjectPath.empty())
                                    ? (SaveScope == PreferenceScopeCategory::Project ? "Save project" : "Save personal")
                                : AppearancePageOpen || InputPage ? "Apply"
                                                                  : "Apply session",
                                ButtonToneCategory::Primary};
    ApplyButton.Disabled = !Dirty || (InputPage && Input.QueryKeyCapture());
    ApplyButton.Height   = 36;
    ButtonStructure DiscardButton{"Discard", ButtonToneCategory::Secondary};
    DiscardButton.Disabled  = !Dirty && !(InputPage && Input.QueryKeyCapture());
    DiscardButton.Height    = 36;
    const auto &MessageText = InputPage ? (!Input.QueryDiagnostic().empty() ? Input.QueryDiagnostic() : InputDiagnostic)
                                        : AppearanceDiagnostic;
    if ((AppearancePageOpen || InputPage) && !MessageText.empty())
    {
        const float MessageLeft = Footer.MinimumX + Pad + (InputPage && Footer.Width() > 500 ? 124 : 0);
        const auto  Message =
            Spanning(MessageLeft, Footer.MinimumY + (StackedFooter ? 32 : 44),
                     std::max(1.0f, (StackedFooter ? Footer.MaximumX - Pad : Discard.MinimumX - 12) - MessageLeft), 20);
        Surface.PushClip(Message);
        ControlKit::TextLeading(Surface, Message, 0, K.TextDim, MessageText.c_str(), 11);
        Surface.PopClip();
    }
    if (ControlKit::PillButton(Surface, Apply, ApplyButton, Pointer).Clicked)
        ActivatePreferenceConfirmation();
    if (ControlKit::PillButton(Surface, Discard, DiscardButton, Pointer).Clicked)
    {
        if (AppearancePageOpen)
            ResetPreferences();
        else if (InputPage)
            Input.ResetDraft();
        else if (WorkspacePage)
        {
            DetachedDraft        = DetachedWindows;
            DockableBrowserDraft = DockableBrowser;
        }
        else
            Notifications.ResetDraft();
    }
    if (InputPage)
    {
        const auto Defaults = Spanning(Footer.MinimumX + Pad, Footer.MinimumY + (StackedFooter ? 58 : 34),
                                       StackedFooter ? std::max(1.0f, Footer.Width() - Pad * 2) : 112, 36);
        DefaultsExtent      = QueryPhysicalExtent(Defaults);
        ButtonStructure DefaultsButton{"Defaults", ButtonToneCategory::Secondary};
        DefaultsButton.Height = 36;
        ControlContact        = ControlContact || ControlKit::Over(Defaults, Pointer);
        if (ControlKit::PillButton(Surface, Defaults, DefaultsButton, Pointer).Clicked)
            Input.ResetDefaults();
    }
    const auto Body =
        Spanning(Card.MinimumX + Pad * 0.5f, Y + 16, Card.Width() - Pad, std::max(1.0f, Footer.MinimumY - Y - 32));
    ConstructScrollableLayout(Body, Pointer);
    ControlKit::OutlineRounded(Surface, Card, K.Stroke, 32);
    if (AppearancePageOpen)
        Appearance.ConstructFloatingLayout(Surface, Pointer, PageAlpha);
    else if (InputPage)
        Input.ConstructFloatingLayout(Surface, Pointer, PageAlpha);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DRAWER ADAPTER
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::ConstructDrawerLayout() noexcept
{
    auto       &IO      = ImGui::GetIO();
    const auto  Display = IO.DisplaySize;
    const auto  Origin  = ImGui::GetMainViewport()->Pos;
    const float Scale   = Appearance.QueryApplied().InterfaceScale / 100.0f;
    Expansion.ConfigureExtent(Display.x, Display.y, Scale);
    const bool Visible = Expansion.QueryHeight() > 0.5f || Expansion.QueryOpen();
    auto       Options = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
    if (!Visible)
        Options |= ImGuiWindowFlags_NoInputs;
    ImGui::SetNextWindowPos(Origin);
    ImGui::SetNextWindowSize(Display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Frontier expansion overlay", nullptr, Options);
    PixelSpace Surface;
    (void)Surface.Begin(SurfaceLayer::Window, Display.x, Display.y, Scale);
    const auto &K      = ControlKit::Palette();
    const float ShadeY = Expansion.QueryHeight() / Scale;
    if (Visible)
    {
        Surface.FillRectangle(Spanning(Origin.x / Scale, Origin.y / Scale, Display.x / Scale, Display.y / Scale),
                              ColorQuad{0, 0, 0, 0.4f * Expansion.QueryProgress()});
        Surface.FillRectangle(Spanning(Origin.x / Scale, Origin.y / Scale, Display.x / Scale, ShadeY), K.Field);
    }
    Expansion.ConstructLayout(Surface, "Frontier", {Origin.x, Origin.y});
    bool CardContact = false;
    if (Visible)
    {
        const bool SubPage =
            ActivePage != ControlCentrePageCategory::QuickControls && ActivePage != ControlCentrePageCategory::Settings;
        const float MaxWidth  = std::max(100.0f, Display.x / Scale - (SubPage ? 48 : 32));
        const float MaxHeight = std::max(100.0f, Display.y / Scale - 84);
        ConfigureCardExtent(
            SubPage ? MaxWidth
                    : std::min(ActivePage == ControlCentrePageCategory::QuickControls ? 920.0f : 420.0f, MaxWidth),
            SubPage ? MaxHeight
                    : std::min(ActivePage == ControlCentrePageCategory::Settings ? 403.0f : 480.0f, MaxHeight));
        DisplayedPage    = PageTime < PageSwapDuration ? DepartingPage : ActivePage;
        const float T    = std::clamp(PageTime < PageSwapDuration ? PageTime / PageSwapDuration
                                                                  : (PageTime - PageSwapDuration) / PageSwapDuration,
                                      0.0f, 1.0f);
        const float Ease = 1 - std::pow(1 - T, 3.0f);
        PageAlpha        = PageTime < PageSwapDuration ? 1 - Ease : Ease;
        const float Direction = Forward ? 1.0f : -1.0f;
        const float OffsetX   = PageTime < PageSwapDuration ? -Direction * 40 * Ease : Direction * 40 * (1 - Ease);
        const float Width     = std::min(static_cast<float>(CardMotion.Spring(CardWidth).Current), MaxWidth);
        const float Height    = std::min(static_cast<float>(CardMotion.Spring(CardHeight).Current), MaxHeight);
        CurrentCard =
            Spanning((Origin.x + Display.x * 0.5f) / Scale - Width * 0.5f + OffsetX,
                     (Origin.y + Display.y * 0.5f) / Scale - Height * 0.5f + ShadeY - Display.y / Scale, Width, Height);
        ControlPointer Pointer;
        Pointer.X        = IO.MousePos.x / Scale;
        Pointer.Y        = IO.MousePos.y / Scale;
        Pointer.Pressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        Pointer.Released = !IO.AppFocusLost && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        Pointer.Down     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        Pointer.Enabled  = !Confirmation.QueryOpen() && !AlertContact && Expansion.QueryOpen() &&
                           !Expansion.QueryDragging() && !QueryPageMoving();
        ControlContact   = false;
        const auto Mark  = Surface.BeginGroup();
        if (DisplayedPage == ControlCentrePageCategory::QuickControls)
            ConstructQuickLayout(Surface, CurrentCard, Pointer);
        else if (DisplayedPage == ControlCentrePageCategory::Settings)
            ConstructCategoryLayout(Surface, CurrentCard, Pointer);
        else
            ConstructPreferenceLayout(Surface, CurrentCard, Pointer);
        Surface.EndGroup(Mark, 0, 0, 1, 0, 0, PageAlpha);
        const auto PhysicalCard = QueryPhysicalExtent(CurrentCard);
        const auto Mouse        = IO.MousePos;
        CardContact             = !AlertContact && PhysicalCard.Encloses(Mouse.x, Mouse.y) && !ControlContact &&
                                  !QueryPageMoving() && (!SubPage || Pointer.Y < CurrentCard.MinimumY + 28);
        const auto NotchBounds  = Expansion.QueryTabExtent();
        const auto Grip         = Expansion.QueryGripExtent();
        if (Pointer.Enabled && Pointer.Pressed && !PhysicalCard.Encloses(Mouse.x, Mouse.y) &&
            !Grip.Encloses(Mouse.x - Origin.x, Mouse.y - Origin.y) &&
            !NotchBounds.Encloses(Mouse.x - Origin.x, Mouse.y - Origin.y))
            Expansion.ConfigureOpen(false);
    }
    Expansion.IntegratePointer(IO.MousePos.x - Origin.x, IO.MousePos.y - Origin.y,
                               !AlertContact && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                               !AlertContact && ImGui::IsMouseDown(ImGuiMouseButton_Left),
                               !AlertContact && ImGui::IsMouseReleased(ImGuiMouseButton_Left), IO.DeltaTime,
                               CardContact);
    if (Expansion.QueryDragging() || Visible ||
        Expansion.QueryTabExtent().Encloses(IO.MousePos.x - Origin.x, IO.MousePos.y - Origin.y))
        ImGui::SetNextFrameWantCaptureMouse(true);
    ImGui::End();
    ImGui::PopStyleVar();
}

namespace
{
ImGuiKey QueryImGuiKey(InputKeyCategory Key) noexcept
{
    if (Key >= InputKeyCategory::A && Key <= InputKeyCategory::Z)
        return static_cast<ImGuiKey>(ImGuiKey_A + static_cast<int>(Key) - static_cast<int>(InputKeyCategory::A));
    if (Key >= InputKeyCategory::Digit0 && Key <= InputKeyCategory::Digit9)
        return static_cast<ImGuiKey>(ImGuiKey_0 + static_cast<int>(Key) - static_cast<int>(InputKeyCategory::Digit0));
    if (Key >= InputKeyCategory::F1 && Key <= InputKeyCategory::F12)
        return static_cast<ImGuiKey>(ImGuiKey_F1 + static_cast<int>(Key) - static_cast<int>(InputKeyCategory::F1));
    switch (Key)
    {
    case InputKeyCategory::Escape:
        return ImGuiKey_Escape;
    case InputKeyCategory::Enter:
        return ImGuiKey_Enter;
    case InputKeyCategory::Space:
        return ImGuiKey_Space;
    case InputKeyCategory::Tab:
        return ImGuiKey_Tab;
    case InputKeyCategory::Comma:
        return ImGuiKey_Comma;
    case InputKeyCategory::Period:
        return ImGuiKey_Period;
    case InputKeyCategory::Home:
        return ImGuiKey_Home;
    case InputKeyCategory::End:
        return ImGuiKey_End;
    case InputKeyCategory::Delete:
        return ImGuiKey_Delete;
    case InputKeyCategory::Left:
        return ImGuiKey_LeftArrow;
    case InputKeyCategory::Right:
        return ImGuiKey_RightArrow;
    case InputKeyCategory::Up:
        return ImGuiKey_UpArrow;
    case InputKeyCategory::Down:
        return ImGuiKey_DownArrow;
    default:
        return ImGuiKey_None;
    }
}
} // namespace

void ControlCentrePanel::IntegrateKeyboard() noexcept
{
    const auto &InputExchange = ImGui::GetIO();
    if (InputExchange.AppFocusLost)
    {
        Input.ResetInteraction();
        return;
    }
    if (Input.QueryKeyCapture())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            Input.ResetKeyCapture();
        else
            for (uint32_t Index = 1; Index < static_cast<uint32_t>(InputKeyCategory::Count); ++Index)
            {
                const auto Key = static_cast<InputKeyCategory>(Index);
                if (ImGui::IsKeyPressed(QueryImGuiKey(Key), false))
                {
                    (void)Input.ConfigureCapturedChord({Key, InputExchange.KeyCtrl, InputExchange.KeyAlt,
                                                        InputExchange.KeyShift, InputExchange.KeySuper});
                    break;
                }
            }
        return; // The captured chord must not also execute a command in this frame.
    }
    // Escape remains an emergency dismissal independent of a saved custom map.
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        Expansion.ConfigureOpen(false);
        Input.ResetInteraction();
        return;
    }
    if (InputExchange.WantTextInput || Input.QueryMenuOpen() || Appearance.QueryMenuOpen() || QueryPageMoving())
        return;
    for (uint32_t Index = 0; Index < InputPreferences::ShortcutCount; ++Index)
    {
        const auto  Command = static_cast<InputCommandCategory>(Index);
        const auto &Chord   = Input.QueryApplied().QueryShortcut(Command);
        if (Chord.Control != InputExchange.KeyCtrl || Chord.Alt != InputExchange.KeyAlt ||
            Chord.Shift != InputExchange.KeyShift || Chord.Super != InputExchange.KeySuper ||
            !ImGui::IsKeyPressed(QueryImGuiKey(Chord.Key), false))
            continue;
        switch (Command)
        {
        case InputCommandCategory::ControlCentre:
            Expansion.ConfigureOpen(!Expansion.QueryOpen());
            break;
        case InputCommandCategory::Typography:
            ActivatePage(AppearancePageCategory::Fonts);
            break;
        case InputCommandCategory::Theme:
            ActivatePage(AppearancePageCategory::Theme);
            break;
        case InputCommandCategory::DismissPanel:
            Expansion.ConfigureOpen(false);
            break;
        default:
            break;
        }
        break;
    }
}

void ControlCentrePanel::ConstructInterface() noexcept
{
    if (QueryPhase() != ActivationPhase::Active)
        return;
    const float Scale         = Appearance.QueryApplied().InterfaceScale / 100.0f;
    const auto &InputExchange = ImGui::GetIO();
    Input.ConfigurePointerDelta({InputExchange.MouseDelta.x / Scale, InputExchange.MouseDelta.y / Scale});
    if (!Confirmation.QueryOpen() && !Browser.QuerySearchActive())
        IntegrateKeyboard();
    if (InputExchange.AppFocusLost)
        AlertPressToken = AlertDismissToken = 0;
    ConstructNotificationStack(false);
    if (BrowserVisible && !BrowserDocked)
        Browser.ConstructInterface(Scale, !AlertContact && !Expansion.QueryOpen() && Expansion.QueryHeight() <= 0.5f &&
                                              !Expansion.QueryDragging());
    ConstructDrawerLayout();
    if (!Expansion.QueryOpen())
    {
        Input.ResetInteraction();
        Appearance.CloseMenus();
    }
    if (Notifications.QueryApplied().ShowFrameRateOverlay)
    {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->Pos.x + ImGui::GetIO().DisplaySize.x - 12,
                                       (AlertVisibleCount ? AlertExtents[AlertVisibleCount - 1].MaximumY * Scale + 12
                                                          : ImGui::GetMainViewport()->Pos.y + 12)),
                                ImGuiCond_Always, ImVec2(1, 0));
        ImGui::Begin("Frame-rate overlay", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
        ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
        ImGui::End();
    }
    ConstructNotificationStack(true);
    const auto Response = Confirmation.Construct(Scale);
    if (Response == ConfirmationResponseCategory::Confirmed && ActivePage == ConfirmationPage &&
        SaveScope == ConfirmationScope)
    {
        if (ConfirmationPage == ControlCentrePageCategory::Appearance)
            (void)FinalizePreferences();
        else if (ConfirmationPage == ControlCentrePageCategory::Input)
            (void)FinalizeInputPreferences();
        else if (ConfirmationPage == ControlCentrePageCategory::Notifications)
            (void)FinalizeNotificationPreferences();
        else if (ConfirmationPage == ControlCentrePageCategory::Workspace)
            (void)FinalizeWorkspacePreferences();
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        AlertPressToken = AlertDismissToken = 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                NOTIFICATION STACK AND HISTORY
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::ActivatePreferenceConfirmation() noexcept
{
    ConfirmationSpecification Specification;
    Specification.Title    = "Apply changes?";
    const bool  Hosting    = ActivePage == ControlCentrePageCategory::Workspace;
    const char *Page       = Hosting                                               ? "workspace hosting"
                             : ActivePage == ControlCentrePageCategory::Appearance ? "appearance"
                             : ActivePage == ControlCentrePageCategory::Input      ? "input"
                                                                                   : "notification";
    const bool  Persistent = Hosting ? PreferencesPath.extension() == ".projectspace"
                                     : SaveScope == PreferenceScopeCategory::Project || !PreferencesPath.empty();
    Specification.Description =
        std::string("Apply these ") + Page + " preferences" +
        (!Hosting && SaveScope == PreferenceScopeCategory::Project ? " and save them to this project?"
         : Persistent                                              ? " and save your personal preferences?"
                                                                   : " for this session?") +
        " Cancel keeps your changes as a draft.";
    if (Confirmation.Activate(Specification))
    {
        ConfirmationPage  = ActivePage;
        ConfirmationScope = SaveScope;
        Appearance.CloseMenus();
        Input.ResetInteraction();
    }
}

void ControlCentrePanel::ConstructNotificationStack(bool Paint) noexcept
{
    if (Confirmation.QueryOpen())
    {
        AlertVisibleCount = 0;
        AlertHoverToken = AlertPressToken = AlertDismissToken = 0;
        AlertContact                                          = true;
        return;
    }
    const auto &IO     = ImGui::GetIO();
    const auto  Origin = ImGui::GetMainViewport()->Pos;
    const float Scale  = Appearance.QueryApplied().InterfaceScale / 100.0f;
    PixelSpace  Surface;
    if (!Surface.Begin(SurfaceLayer::Above, IO.DisplaySize.x, IO.DisplaySize.y, Scale))
        return;
    const float Width       = std::min(420.0f, IO.DisplaySize.x / Scale - 24);
    const float X           = (Origin.x + IO.DisplaySize.x) / Scale - Width - 12;
    float       Y           = Origin.y / Scale + 12;
    const auto  Tab         = Expansion.QueryTabExtent();
    const auto &Pending     = Alerts.QueryPending();
    const float FirstHeight = Pending.empty() ? 0 : NotificationCard::QueryHeight(Surface, Width, Pending.front());
    // 📝 A narrow viewport must not place a card inside the shared expansion tab.
    if (X * Scale < Origin.x + Tab.MaximumX && (X + Width) * Scale > Origin.x + Tab.MinimumX &&
        Y * Scale < Origin.y + Tab.MaximumY && (Y + FirstHeight) * Scale > Origin.y + Tab.MinimumY)
        Y = (Origin.y + Tab.MaximumY) / Scale + 12;
    AlertVisibleCount       = 0;
    AlertHoverToken         = 0;
    AlertContact            = AlertPressToken != 0;
    uint64_t ReclaimedToken = 0;
    for (size_t Index = 0; NotificationsEnabled && Width >= 130 && Index < Pending.size() &&
                           Index < NotificationSequence::VisibleCapacity;
         ++Index)
    {
        const auto &Entry  = Pending[Index];
        const float Height = NotificationCard::QueryHeight(Surface, Width, Entry);
        if (Y + Height > (Origin.y + IO.DisplaySize.y) / Scale - 12)
            break;
        const auto Extent                 = Spanning(X, Y, Width, Height);
        AlertExtents[AlertVisibleCount++] = Extent;
        ControlPointer Pointer;
        Pointer.X        = IO.MousePos.x / Scale;
        Pointer.Y        = IO.MousePos.y / Scale;
        Pointer.Pressed  = !IO.AppFocusLost && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        Pointer.Down     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        Pointer.Released = !IO.AppFocusLost && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        Pointer.Enabled  = true;
        if (Extent.Encloses(Pointer.X, Pointer.Y))
        {
            AlertHoverToken = Entry.Token;
            AlertContact    = true;
            if (!Paint && Pointer.Pressed)
            {
                AlertPressToken   = Entry.Token;
                const auto Close  = Spanning(Extent.MaximumX - 22 - ControlKit::RoundIconDiameter / 2,
                                             Extent.MinimumY + 29 - ControlKit::RoundIconDiameter / 2,
                                             ControlKit::RoundIconDiameter, ControlKit::RoundIconDiameter);
                AlertDismissToken = Close.Encloses(Pointer.X, Pointer.Y) ? Entry.Token : 0;
            }
        }
        if (Paint)
        {
            // 📝 A real input window also protects native ImGui scrolling beneath the custom card.
            const std::string Name = "Notification##" + std::to_string(Entry.Token);
            ImGui::SetNextWindowPos(ImVec2(Extent.MinimumX * Scale, Extent.MinimumY * Scale));
            ImGui::SetNextWindowSize(ImVec2(Extent.Width() * Scale, Extent.Height() * Scale));
            ImGui::Begin(Name.c_str(), nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoMove);
            if (NotificationCard::Construct(Surface, Extent, Entry, Pointer, true) && AlertDismissToken == Entry.Token)
                ReclaimedToken = Entry.Token;
            ImGui::End();
        }
        Y += Height + 12;
    }
    if (AlertContact)
        ImGui::SetNextFrameWantCaptureMouse(true);
    if (ReclaimedToken)
        Alerts.Reclaim(ReclaimedToken);
}

void ControlCentrePanel::ConstructNotificationStream(const PlaneExtent &Body, const ControlPointer &Pointer) noexcept
{
    StreamExtent      = QueryPhysicalExtent(Body);
    const float Scale = Appearance.QueryApplied().InterfaceScale / 100.0f;
    ImGui::SetCursorScreenPos(ImVec2(Body.MinimumX * Scale, Body.MinimumY * Scale));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, PageAlpha);
    ImGui::BeginChild("Notification Stream", ImVec2(Body.Width() * Scale, Body.Height() * Scale), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    PixelSpace Content;
    if (Content.Begin(SurfaceLayer::Window, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, Scale))
    {
        const auto Start   = ImGui::GetCursorScreenPos();
        const auto Column  = Spanning(Start.x / Scale, Start.y / Scale,
                                      std::max(1.0f, (ImGui::GetContentRegionAvail().x - 12) / Scale), 10000);
        auto       Local   = Pointer;
        Local.Enabled      = Local.Enabled && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const auto  Mark   = Content.BeginGroup();
        const float Height = ConstructNotificationHistory(Content, Column, 0, Local);
        Content.EndGroup(Mark, 0, 0, 1, 0, 0, PageAlpha);
        ImGui::Dummy(ImVec2(Column.Width() * Scale, Height * Scale));
    }
    ControlContact = ControlContact || Body.Encloses(Pointer.X, Pointer.Y);
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

float ControlCentrePanel::ConstructNotificationHistory(PixelSpace &Surface, const PlaneExtent &Column, float Offset,
                                                       const ControlPointer &Pointer) noexcept
{
    const auto       &Palette = ControlKit::Palette();
    const auto       &History = Alerts.QueryHistory();
    const std::string Heading = "Notification Stream (" + std::to_string(History.size()) + ")";
    Surface.Text(Column.MinimumX, Column.MinimumY + Offset + 9, Palette.Text, Heading.c_str(), 14);
    const bool Stacked = Column.Width() < 360;
    const auto Clear   = Spanning(Column.MaximumX - 104, Column.MinimumY + Offset + (Stacked ? 32 : 0), 104, 32);
    HistoryClearExtent = QueryPhysicalExtent(Clear);
    if (!History.empty() && ControlKit::ChipButton(Surface, Clear, "Clear history", false, Pointer).Clicked)
    {
        Alerts.ResetHistory();
        return Offset + (Stacked ? 84 : 48);
    }
    Offset += Stacked ? 84 : 48;
    if (History.empty())
    {
        Surface.Text(Column.MinimumX, Column.MinimumY + Offset, Palette.TextDim, "No notifications in this session.",
                     13);
        return Offset + 32;
    }
    for (auto Entry = History.rbegin(); Entry != History.rend(); ++Entry)
    {
        const float Height = NotificationCard::QueryHeight(Surface, Column.Width(), *Entry);
        (void)NotificationCard::Construct(Surface,
                                          Spanning(Column.MinimumX, Column.MinimumY + Offset, Column.Width(), Height),
                                          *Entry, Pointer, false);
        Offset += Height + 12;
    }
    return Offset;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 PROJECTSPACE PREFERENCE SCOPE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
bool InstalledDefaultPath(const std::filesystem::path &Path)
{
    if (Path.empty())
        return false;
    const auto Defaults  = std::filesystem::weakly_canonical(ResolveContentDirectory("EngineContent/Defaults"));
    const auto Candidate = std::filesystem::weakly_canonical(Path);
    auto       Prefix    = Defaults.begin();
    auto       Segment   = Candidate.begin();
    while (Prefix != Defaults.end() && Segment != Candidate.end() && *Prefix == *Segment)
    {
        ++Prefix;
        ++Segment;
    }
    return Prefix == Defaults.end();
}
} // namespace

bool ControlCentrePanel::ConfigurePreferenceScope(PreferenceScopeCategory Scope) noexcept
{
    if ((Scope == PreferenceScopeCategory::Project && !ProjectWritable) ||
        (Scope == PreferenceScopeCategory::Personal && PreferencesPath.empty()))
        return false;
    SaveScope = Scope;
    return true;
}

bool ControlCentrePanel::ReloadProfiles() noexcept
{
    if (Appearance.QueryDirty() || Input.QueryDirty() || Notifications.QueryDirty() || QueryWorkspaceDirty())
    {
        Diagnostic = "Apply or discard drafts before reloading profiles.";
        return false;
    }
    try
    {
        std::string          Warnings;
        ProjectSpaceDocument Defaults;
        Defaults.Sections =
            toml::table{{"format", "frontier.projectspace.editor"},
                        {"version", 1},
                        {"project", toml::table{{"identity", "emergency-defaults"}, {"name", "Emergency defaults"}}},
                        {"content_roots", toml::array{"Content"}},
                        {"levels", toml::array{}},
                        {"catalogue", toml::array{}},
                        {"dependency_pins", toml::array{}},
                        {"engine", toml::table{}},
                        {"workspace", toml::table{}}};
        Defaults.AppearancePresent = Defaults.InputPresent = Defaults.NotificationsPresent = true;
        const auto Template = ResolveContentDirectory("EngineContent/Defaults") / "BlankProject/Blank.projectspace";
        if (!ProjectSpaceCodec::Decode(Template, Defaults, Diagnostic))
            Warnings = "Engine defaults unavailable; using built-in defaults. ";
        EngineDefaults       = Defaults;
        auto       Effective = Defaults;
        const auto Overlay   = [&](const ProjectSpaceDocument &Layer)
        {
            if (Layer.AppearancePresent)
                Effective.Appearance = Layer.Appearance;
            if (Layer.InputPresent)
                Effective.Input = Layer.Input;
            if (Layer.NotificationsPresent)
                Effective.Notifications = Layer.Notifications;
        };
        ProjectWritable = false;
        if (!ProjectPath.empty())
        {
            ProjectSpaceDocument Project;
            if (ProjectSpaceCodec::Decode(ProjectPath, Project, Diagnostic))
            {
                Overlay(Project);
                ProjectWritable = !InstalledDefaultPath(ProjectPath);
            }
            else
                return false;
        }
        PersonalOverrides = false;
        DetachedWindows = DetachedDraft = false;
        DockableBrowser = DockableBrowserDraft = false;
        if (PreferencesPath.extension() == ".projectspace" && std::filesystem::exists(PreferencesPath))
        {
            ProjectSpaceDocument Personal;
            if (ProjectSpaceCodec::Decode(PreferencesPath, Personal, Diagnostic))
            {
                DetachedWindows = DetachedDraft =
                    Personal.Sections["workspace"]["hosting"]["allow_detached_windows"].value_or(false);
                DockableBrowser = DockableBrowserDraft =
                    Personal.Sections["workspace"]["hosting"]["dockable_content_browser"].value_or(false);
                PersonalOverrides = Personal.Sections["engine"]["personal_overrides"].value_or(false);
                if (ProjectPath.empty() || PersonalOverrides)
                    Overlay(Personal);
            }
            else
                Warnings += "Personal overrides refused: " + Diagnostic + ". ";
        }
        RequestedFamily                             = Effective.Appearance.RequestedFamily;
        const auto Family                           = Typefaces.ResolveFamilyIndex(RequestedFamily);
        FamilyUnavailable                           = Family < 0;
        FallbackFamily                              = Family >= 0 ? static_cast<uint32_t>(Family) : 0;
        Effective.Appearance.Preferences.FontFamily = FallbackFamily;
        if (FamilyUnavailable)
            Warnings += "Requested font unavailable; fallback active, saved family retained. ";
        Appearance.Seed(Effective.Appearance.Preferences);
        Input.InitializePreferences(Effective.Input);
        Notifications.InitializePreferences(Effective.Notifications);
        SaveScope            = ProjectWritable ? PreferenceScopeCategory::Project : PreferenceScopeCategory::Personal;
        AppliedRevision      = ~0u;
        Diagnostic           = Warnings;
        AppearanceDiagnostic = InputDiagnostic = Diagnostic;
        return true;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool ControlCentrePanel::PersistWorkspaceSection(ControlCentrePageCategory Page) noexcept
{
    try
    {
        const bool  Hosting = Page == ControlCentrePageCategory::Workspace;
        const auto &Target  = !Hosting && SaveScope == PreferenceScopeCategory::Project ? ProjectPath : PreferencesPath;
        if (Target.empty() || InstalledDefaultPath(Target))
        {
            Diagnostic = "Choose a writable personal or project container; installed defaults are read-only.";
            return false;
        }
        ProjectSpaceDocument Candidate;
        if (std::filesystem::exists(Target))
        {
            if (!ProjectSpaceCodec::Decode(Target, Candidate, Diagnostic))
                return false;
        }
        else
        {
            if (!Hosting && SaveScope == PreferenceScopeCategory::Project)
            {
                Diagnostic = "Project no longer exists.";
                return false;
            }
            Candidate = EngineDefaults;
            Candidate.Sections.insert_or_assign("workspace", toml::table{});
            Candidate.AppearancePresent = Candidate.InputPresent = Candidate.NotificationsPresent = false;
            auto &Project = *Candidate.Sections["project"].as_table();
            Project.insert_or_assign("identity", "personal-workspace");
            Project.insert_or_assign("name", "Personal workspace preferences");
        }
        if (Page == ControlCentrePageCategory::Appearance)
        {
            Candidate.Appearance.Preferences = Appearance.QueryDraft();
            const auto *Family               = Typefaces.QueryFamily(Appearance.QueryDraft().FontFamily);
            Candidate.Appearance.RequestedFamily =
                FamilyUnavailable && Appearance.QueryDraft().FontFamily == FallbackFamily ? RequestedFamily
                : Family                                                                  ? Family->Name
                                                                                          : RequestedFamily;
            Candidate.AppearancePresent = true;
        }
        else if (Page == ControlCentrePageCategory::Input)
        {
            Candidate.Input        = Input.QueryDraft();
            Candidate.InputPresent = true;
        }
        else if (Hosting)
        {
            auto &Workspace = *Candidate.Sections["workspace"].as_table();
            auto  Profile   = Workspace["hosting"].as_table() ? *Workspace["hosting"].as_table() : toml::table{};
            Profile.insert_or_assign("format", 1);
            Profile.insert_or_assign("allow_detached_windows", DetachedDraft);
            Profile.insert_or_assign("dockable_content_browser", DockableBrowserDraft);
            Workspace.insert_or_assign("hosting", std::move(Profile));
        }
        else
        {
            Candidate.Notifications        = Notifications.QueryDraft();
            Candidate.NotificationsPresent = true;
        }
        if (!Hosting && SaveScope == PreferenceScopeCategory::Personal)
            Candidate.Sections["engine"].as_table()->insert_or_assign("personal_overrides", true);
        if (!ProjectSpaceCodec::Encode(Target, Candidate, Diagnostic))
            return false;
        if (!Hosting && SaveScope == PreferenceScopeCategory::Personal)
            PersonalOverrides = true;
        if (Page == ControlCentrePageCategory::Appearance)
        {
            RequestedFamily   = Candidate.Appearance.RequestedFamily;
            FamilyUnavailable = Typefaces.ResolveFamilyIndex(RequestedFamily) < 0;
            FallbackFamily    = Appearance.QueryDraft().FontFamily;
        }
        Diagnostic = Hosting                                         ? "Saved personal window-hosting preferences."
                     : SaveScope == PreferenceScopeCategory::Project ? "Saved to projectspace."
                                                                     : "Saved personal overrides.";
        return true;
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool ControlCentrePanel::ConfigurePersonalOverrides(bool Enabled) noexcept
{
    if (Appearance.QueryDirty() || Input.QueryDirty() || Notifications.QueryDirty() || QueryWorkspaceDirty())
    {
        Diagnostic = "Apply or discard drafts before changing override precedence.";
        return false;
    }
    try
    {
        ProjectSpaceDocument Personal;
        if (!ProjectSpaceCodec::Decode(PreferencesPath, Personal, Diagnostic))
            return false;
        if (InstalledDefaultPath(PreferencesPath))
        {
            Diagnostic = "Installed defaults are read-only.";
            return false;
        }
        Personal.Sections["engine"].as_table()->insert_or_assign("personal_overrides", Enabled);
        if (!ProjectSpaceCodec::Encode(PreferencesPath, Personal, Diagnostic))
            return false;
        return ReloadProfiles();
    }
    catch (const std::exception &Failure)
    {
        Diagnostic = Failure.what();
        return false;
    }
}

bool ControlCentrePanel::FinalizeNotificationPreferences() noexcept
{
    if ((SaveScope == PreferenceScopeCategory::Project || PreferencesPath.extension() == ".projectspace") &&
        !PersistWorkspaceSection(ControlCentrePageCategory::Notifications))
    {
        BroadcastNotice(true);
        return false;
    }
    Notifications.FinalizePreferences();
    if (SaveScope != PreferenceScopeCategory::Project && PreferencesPath.extension() != ".projectspace")
        Diagnostic = "Notification preferences applied for this session.";
    BroadcastNotice(false,
                    SaveScope != PreferenceScopeCategory::Project && PreferencesPath.extension() != ".projectspace");
    return true;
}

float ControlCentrePanel::ConstructWorkspaceSettings(PixelSpace &Surface, const PlaneExtent &Column,
                                                     const ControlPointer &Pointer) noexcept
{
    const auto &Palette = ControlKit::Palette();
    const bool  Narrow  = Column.Width() < 440;
    Surface.Text(Column.MinimumX + 16, Column.MinimumY + 8, Palette.Text, "Content Browser", 14);
    const float ChoiceWidth = Surface.MeasureText("Draggable", 13).X + Surface.MeasureText("Dockable", 13).X + 76;
    const auto  Choice      = Spanning(Column.MinimumX + 16, Column.MinimumY + 40, ChoiceWidth, 32);
    BrowserHostingExtent    = QueryPhysicalExtent(Choice);
    const char *Labels[]    = {"Draggable", "Dockable"};
    uint32_t    Chosen      = DockableBrowserDraft ? 1 : 0;
    if (ControlKit::Segmented(Surface, Choice, Labels, 2, Chosen, Pointer, Chosen).Clicked)
        DockableBrowserDraft = Chosen == 1;
    Surface.Text(Column.MinimumX + 16, Column.MinimumY + 88, Palette.TextDim, "Draggable by default.", 13);
    const float SectionY = Column.MinimumY + 136;
    Surface.Text(Column.MinimumX + 16, SectionY + 8, Palette.Text, "Allow detached OS windows", 14);
    const auto Toggle    = Spanning(Narrow ? Column.MinimumX + 16 : Column.MaximumX - 64, SectionY + (Narrow ? 42 : 4),
                                    ControlKit::SwitchWidth, ControlKit::SwitchHeight);
    DetachedToggleExtent = QueryPhysicalExtent(Toggle);
    if (ControlKit::Switch(Surface, Toggle.MinimumX, Toggle.MinimumY, DetachedDraft, Pointer).Clicked)
        DetachedDraft = !DetachedDraft;
    const float TextY = SectionY + (Narrow ? 88 : 52);
    Surface.PushClip(Spanning(Column.MinimumX + 16, TextY, Column.Width() - 32, 140));
    Surface.Text(Column.MinimumX + 16, TextY, Palette.TextDim, "Disabled by default.", 13);
    Surface.Text(Column.MinimumX + 16, TextY + 24, Palette.TextDim, "Optional for multiple monitors.", 13);
    Surface.Text(Column.MinimumX + 16, TextY + 56, Palette.TextDim, "Off returns panels to the main window.", 13);
    Surface.Text(Column.MinimumX + 16, TextY + 88, Palette.TextDim, "Personal preferences only.", 13);
    Surface.PopClip();
    const float     FooterY = TextY + 128;
    ButtonStructure ResetButton{"Reset saved layout", ButtonToneCategory::Secondary};
    const auto      Reset = Spanning(Column.MinimumX + 16, FooterY, 180, 32);
    LayoutResetExtent     = QueryPhysicalExtent(Reset);
    if (ControlKit::PillButton(Surface, Reset, ResetButton, Pointer).Clicked)
        RequestLayoutReset();
    Surface.PushClip(Spanning(Column.MinimumX + 16, FooterY + 40, Column.Width() - 32, 48));
    Surface.Text(Column.MinimumX + 16, FooterY + 40, Palette.TextDim,
                 LayoutDiagnostic.empty() ? "Layout saves automatically." : LayoutDiagnostic.c_str(), 12);
    Surface.PopClip();
    return FooterY - Column.MinimumY + 100;
}

bool ControlCentrePanel::PersistWorkspaceLayout(const toml::table &Layout, std::string &Message,
                                              std::string_view LayoutKey) noexcept
{
    try
    {
        if (LayoutKey.empty() || LayoutKey.size() > 64 ||
            LayoutKey.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string_view::npos)
        {
            Message = "Invalid workspace layout key.";
            return false;
        }
        if (!PanelDockspace::ValidateLayout(Layout, Message))
            return false;
        if (PreferencesPath.extension() != ".projectspace" || InstalledDefaultPath(PreferencesPath))
        {
            Message = "Layout is session-only: no writable personal projectspace.";
            return false;
        }
        ProjectSpaceDocument Candidate;
        if (std::filesystem::exists(PreferencesPath))
        {
            if (!ProjectSpaceCodec::Decode(PreferencesPath, Candidate, Message))
                return false;
        }
        else
        {
            Candidate = EngineDefaults;
            Candidate.Sections.insert_or_assign("workspace", toml::table{});
            Candidate.AppearancePresent = Candidate.InputPresent = Candidate.NotificationsPresent = false;
            auto &Project = *Candidate.Sections["project"].as_table();
            Project.insert_or_assign("identity", "personal-workspace");
            Project.insert_or_assign("name", "Personal workspace preferences");
            auto &Workspace = *Candidate.Sections["workspace"].as_table();
            Workspace.insert("hosting", toml::table{{"format", 1},
                                                    {"allow_detached_windows", DetachedWindows},
                                                    {"dockable_content_browser", DockableBrowser}});
        }
        Candidate.Sections["workspace"].as_table()->insert_or_assign(LayoutKey, Layout);
        return ProjectSpaceCodec::Encode(PreferencesPath, Candidate, Message);
    }
    catch (const std::exception &Failure)
    {
        Message = Failure.what();
        return false;
    }
}

bool ControlCentrePanel::FinalizeWorkspacePreferences() noexcept
{
    const bool Persistent = PreferencesPath.extension() == ".projectspace";
    if (Persistent && !PersistWorkspaceSection(ControlCentrePageCategory::Workspace))
    {
        BroadcastNotice(true);
        return false;
    }
    DetachedWindows = DetachedDraft;
    DockableBrowser = DockableBrowserDraft;
    if (!Persistent)
        Diagnostic = "Window-hosting preferences applied for this session.";
    BroadcastNotice(false, !Persistent);
    return true;
}

void ControlCentrePanel::ConstructHostedNotificationStream() noexcept
{
    const float    Scale     = Appearance.QueryApplied().InterfaceScale / 100.0f;
    const auto     Position  = ImGui::GetCursorScreenPos();
    const auto     Available = ImGui::GetContentRegionAvail();
    ControlPointer Pointer;
    Pointer.X        = ImGui::GetIO().MousePos.x / Scale;
    Pointer.Y        = ImGui::GetIO().MousePos.y / Scale;
    Pointer.Pressed  = ImGui::IsMouseClicked(0);
    Pointer.Released = ImGui::IsMouseReleased(0);
    Pointer.Down     = ImGui::IsMouseDown(0);
    Pointer.Enabled  = !ImGui::GetIO().AppFocusLost && !Confirmation.QueryOpen() &&
                       (!Expansion.QueryOpen() || ImGui::GetWindowViewport() != ImGui::GetMainViewport()) &&
                       ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ConstructNotificationStream(
        Spanning(Position.x / Scale, Position.y / Scale, Available.x / Scale, Available.y / Scale), Pointer);
}

} // namespace Frontier
