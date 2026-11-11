/*============================================================================================================================================
                                                  CONTROLCENTREVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation and 14-image CPU visual snapshot suite for ControlCentrePanel, reusable Controls, and .projectspace persistence

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypefaceCatalog.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Components/Docking/WorkspaceDockContainer.h"
#include "Exhibits/SoftwareCanvasRasterizer.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{

using Frontier::AccentPreset;
using Frontier::AppearanceSectionTab;
using Frontier::AppearanceSettings;
using Frontier::ButtonDescriptor;
using Frontier::ButtonTone;
using Frontier::ConfirmationSpecification;
using Frontier::ControlCentrePage;
using Frontier::ControlCentrePanel;
using Frontier::ControlColorSet;
using Frontier::ControlPointer;
using Frontier::DialogueLayoutStyle;
using Frontier::DialogueSeverity;
using Frontier::InputPreferences;
using Frontier::NotificationPreferences;
using Frontier::NotificationSeverity;
using Frontier::PixelSpace;
using Frontier::ProjectSpaceArchive;
using Frontier::ProjectSpaceDocument;
using Frontier::Rectangle2D;
using Frontier::ShortcutChord;
using Frontier::ShortcutKey;
using Frontier::TextEditBuffer;
using Frontier::ThemeCatalog;
using Frontier::ThemePreset;
using Frontier::TypefaceCatalog;
using Frontier::Components::ConfigureImGuiWorkspaceTheme;
using Frontier::Components::DockPlacementDirection;
using Frontier::Components::HeadlessWorkspaceDockContainer;
using Frontier::Components::HostWindowToken;
using Frontier::Components::PaneDirectiveMask;
using Frontier::Components::RgbaColor8;
using Frontier::Components::WorkspaceDockPaneDescriptor;
using Frontier::Exhibits::ResolveRepositoryRootPath;
using Frontier::Exhibits::SoftwareRasterCanvas;
using Frontier::Windowing::WindowConfigurationDescriptor;

namespace CapsuleButton        = Frontier::CapsuleButton;
namespace ControlStyle         = Frontier::ControlStyle;
namespace DropdownSelector     = Frontier::DropdownSelector;
namespace InputSettingsCatalog = Frontier::InputSettingsCatalog;
namespace TextInputField       = Frontier::TextInputField;

//------------------------------------------------------------------------------------------------------------------------
//                                                  VALIDATION ASSERTIONS
//------------------------------------------------------------------------------------------------------------------------

bool VerifyAssertion(bool Condition, const char* Description) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[ControlCentreValidation] FAILED: %s\n", Description);
        return false;
    }
    return true;
}

bool ValidateProjectSpaceContainerAndPascalCaseSchema()
{
    std::string Diagnostic{};

    if (!VerifyAssertion(
            !ProjectSpaceArchive::ValidateContainerPath("/tmp/BadConfig.toml", Diagnostic),
            "ProjectSpaceArchive rejects .toml container paths"))
    {
        return false;
    }

    const std::filesystem::path TemporaryArchive = std::filesystem::temp_directory_path() / "FrontierValidation.projectspace";
    std::error_code             IoFailure{};
    std::filesystem::remove(TemporaryArchive, IoFailure);

    ProjectSpaceDocument OutboundDocument{};
    OutboundDocument.ProjectTitle                           = "FrontierStudio";
    OutboundDocument.Appearance.InterfaceScale              = 115.0f;
    OutboundDocument.Appearance.Theme                       = ThemePreset::Dracula;
    OutboundDocument.Appearance.CornerRadius                = 20.0f;
    OutboundDocument.Appearance.Accent                      = AccentPreset::Violet;
    OutboundDocument.Appearance.WarningSwatch               = 2u;
    OutboundDocument.Appearance.SuccessSwatch               = 1u;
    OutboundDocument.Appearance.NoticeSwatch                = 3u;
    OutboundDocument.Appearance.CautionSwatch               = 1u;
    OutboundDocument.TypefaceName                           = "Space Grotesk";
    OutboundDocument.WorkspaceLayout.DetachedWindowsEnabled = true;
    OutboundDocument.Input.MouseSensitivity                 = 68.0f;
    OutboundDocument.Input.CustomShortcutsEnabled           = true;
    OutboundDocument.Input.AdvancedControlsEnabled          = true;
    OutboundDocument.Input.InvertedPitchEnabled             = true;
    OutboundDocument.Notifications.RateOverlayEnabled       = true;
    OutboundDocument.Notifications.AppliedAlertsEnabled     = false;
    OutboundDocument.Notifications.ErrorAlertsEnabled       = true;
    OutboundDocument.Notifications.AlertHoldSeconds         = 5.5f;

    if (!VerifyAssertion(ProjectSpaceArchive::Save(TemporaryArchive, OutboundDocument, Diagnostic), "Save .projectspace container"))
    {
        return false;
    }

    {
        std::ifstream     Stream(TemporaryArchive);
        const std::string Content((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
        if (!VerifyAssertion(
                Content.find("['.visualappearance']") != std::string::npos &&
                Content.find("['.FontSpace']") != std::string::npos &&
                Content.find("['.workspacelayout']") != std::string::npos &&
                Content.find("['.inputsettings']") != std::string::npos &&
                Content.find("['.notificationsettings']") != std::string::npos &&
                Content.find("PersonalAppearance") == std::string::npos &&
                Content.find('_') == std::string::npos,
                ".projectspace uses 100% PascalCase keys with zero snake_case and zero PersonalAppearance"))
        {
            return false;
        }
    }

    ProjectSpaceDocument InboundDocument{};
    if (!VerifyAssertion(ProjectSpaceArchive::Load(TemporaryArchive, InboundDocument, Diagnostic), "Load .projectspace container"))
    {
        return false;
    }

    if (!VerifyAssertion(
            InboundDocument.Appearance.Theme == ThemePreset::Dracula &&
            InboundDocument.Appearance.Accent == AccentPreset::Violet &&
            std::abs(InboundDocument.Appearance.InterfaceScale - 115.0f) < 0.01f &&
            InboundDocument.TypefaceName == "Space Grotesk" &&
            InboundDocument.WorkspaceLayout.DetachedWindowsEnabled &&
            std::abs(InboundDocument.Input.MouseSensitivity - 68.0f) < 0.01f &&
            InboundDocument.Input.CustomShortcutsEnabled &&
            InboundDocument.Input.InvertedPitchEnabled &&
            InboundDocument.Notifications.RateOverlayEnabled &&
            std::abs(InboundDocument.Notifications.AlertHoldSeconds - 5.5f) < 0.01f,
            "Round-trip .projectspace fields match 1:1"))
    {
        return false;
    }

    std::filesystem::remove(TemporaryArchive, IoFailure);
    return true;
}

bool ValidateInputShortcutConflictDetection()
{
    InputPreferences DuplicatePreferences{};
    DuplicatePreferences.CustomShortcutsEnabled = true;
    DuplicatePreferences.Shortcuts[1]           = DuplicatePreferences.Shortcuts[0];

    std::string Diagnostic{};
    if (!VerifyAssertion(
            !InputSettingsCatalog::ValidatePreferences(DuplicatePreferences, Diagnostic) && !Diagnostic.empty(),
            "Duplicate shortcut chords rejected with diagnostic"))
    {
        return false;
    }

    InputPreferences BareLetterPreferences{};
    BareLetterPreferences.CustomShortcutsEnabled = true;
    BareLetterPreferences.Shortcuts[1]           = ShortcutChord{ ShortcutKey::K, false, false, false, false };
    if (!VerifyAssertion(
            !InputSettingsCatalog::ValidatePreferences(BareLetterPreferences, Diagnostic),
            "Modifierless printable letter shortcut rejected"))
    {
        return false;
    }

    InputPreferences ValidPreferences{};
    ValidPreferences.CustomShortcutsEnabled = true;
    ValidPreferences.Shortcuts[1]           = ShortcutChord{ ShortcutKey::K, true, false, true, false };
    if (!VerifyAssertion(
            InputSettingsCatalog::ValidatePreferences(ValidPreferences, Diagnostic) &&
            InputSettingsCatalog::FormatChordLabel(ValidPreferences.Shortcuts[1]) == "Ctrl + Shift + K",
            "Ctrl + Shift + K chord validates and formats cleanly"))
    {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              VISUAL SNAPSHOT SUITE GENERATION
//------------------------------------------------------------------------------------------------------------------------

bool RenderSnapshotSuite(const std::filesystem::path& OutputDirectory)
{
    IMGUI_CHECKVERSION();
    ImGuiContext* GuiSession = ImGui::CreateContext();
    ImGui::SetCurrentContext(GuiSession);

    ImGuiIO& Io    = ImGui::GetIO();
    Io.IniFilename = nullptr;
    Io.LogFilename = nullptr;
    Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    const auto      RepositoryRoot = ResolveRepositoryRootPath();
    TypefaceCatalog Typefaces{};
    if (!VerifyAssertion(
            Typefaces.LoadArchive(RepositoryRoot / "EngineContent/FontsArchives") == 5u,
            "Load FontCatalogue.FontSpace (zero .toml)"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    TypefaceCatalog::AssignCurrent(&Typefaces);
    if (ImFont* DefaultFace = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular))
    {
        Io.FontDefault = DefaultFace;
    }

    unsigned char* TexturePixels = nullptr;
    int            TextureWidth  = 0;
    int            TextureHeight = 0;
    Io.Fonts->GetTexDataAsRGBA32(&TexturePixels, &TextureWidth, &TextureHeight);

    constexpr uint32_t CanvasWidth  = 1280u;
    constexpr uint32_t CanvasHeight = 800u;

    HeadlessWorkspaceDockContainer Dock{};
    const auto WindowDescriptor = WindowConfigurationDescriptor::ConstructApplicationWindow("Frontier", CanvasWidth, CanvasHeight);
    if (!Dock.Initialize(WindowDescriptor))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    (void)Dock.RegisterPane(WorkspaceDockPaneDescriptor{
        "Outliner",
        DockPlacementDirection::SplitLeft,
        0.22f,
        PaneDirectiveMask::StandardPaneDirectives,
        {},
    });
    (void)Dock.RegisterPane(WorkspaceDockPaneDescriptor{
        "Viewport",
        DockPlacementDirection::CenterTabGroup,
        0.56f,
        PaneDirectiveMask::CentralViewportDirectives,
        {},
    });
    (void)Dock.RegisterPane(WorkspaceDockPaneDescriptor{
        "Inspector",
        DockPlacementDirection::SplitRight,
        0.28f,
        PaneDirectiveMask::StandardPaneDirectives,
        {},
    });

    ControlCentrePanel Panel{};
    Panel.AttachTypefaces(&Typefaces);

    auto CaptureSnapshot = [&](const char* FileName, RgbaColor8 ClearColor = RgbaColor8{ 0, 0, 0, 255 }) -> bool
    {
        ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Panel.QueryThemeCatalog());
        for (int Iteration = 0; Iteration < 3; ++Iteration)
        {
            Io.DisplaySize = ImVec2(static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight));
            Io.DeltaTime   = 1.0f / 60.0f;
            ImGui::NewFrame();
            Dock.PresentWindowImGuiPanes(HostWindowToken{ 0u });
            Panel.Render({ 0.0f, 0.0f, static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight) }, 1.0 / 60.0, 60.0f);
            ImGui::Render();
        }
        SoftwareRasterCanvas Canvas(CanvasWidth, CanvasHeight, ClearColor);
        Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        return VerifyAssertion(Canvas.WritePngFile(OutputDirectory / FileName), FileName);
    };

    // 01: Closed top "Frontier" notch over blank Outliner / Viewport / Inspector
    Panel.AccessDraftAppearance().Theme = ThemePreset::Dark;
    Panel.ApplyDraftAppearanceImmediate();
    Panel.AccessNotificationStream().ClearHistory();
    Panel.AccessNotificationStream().ClearPending();
    Panel.ConfigureOpen(false, true);
    if (!CaptureSnapshot("01-ClosedNotch.png", RgbaColor8{ 17, 17, 17, 255 }))
    {
        return false;
    }

    // Switch to default Oled theme for Control Centre inspection
    Panel.AccessDraftAppearance().Theme = ThemePreset::Oled;
    Panel.ApplyDraftAppearanceImmediate();
    Panel.AccessNotificationStream().ClearHistory();
    Panel.AccessNotificationStream().ClearPending();

    Panel.AccessNotificationStream().Broadcast(
        NotificationSeverity::Notice,
        "ProjectSpace synchronized",
        "Loaded .visualappearance, .FontSpace, .workspacelayout, .inputsettings, and .notificationsettings.",
        4.0f,
        false);
    Panel.AccessNotificationStream().Broadcast(
        NotificationSeverity::Success,
        "Preferences saved",
        "Saved appearance and typography to FrontierStudio.projectspace.",
        4.0f,
        false);
    Panel.AccessNotificationStream().Broadcast(
        NotificationSeverity::Warning,
        "Detached OS windows disabled",
        "Enable detached OS windows in Workspace settings to tear panes onto secondary monitors.",
        4.0f,
        false);
    Panel.AccessNotificationStream().Broadcast(
        NotificationSeverity::Error,
        "Shortcut conflict blocked",
        "'Typography' and 'Control Centre' cannot share Ctrl + Period.",
        4.0f,
        false);

    // 02: Quick Controls + Notification Stream
    Panel.ConfigureOpen(true, true);
    Panel.ActivatePage(ControlCentrePage::QuickControls, true);
    if (!CaptureSnapshot("02-QuickPanel.png"))
    {
        return false;
    }

    // 03: Settings Directory (4 rows: Appearance, Input, Notifications, Workspace — zero Content Browser)
    Panel.ActivatePage(ControlCentrePage::Settings, true);
    if (!CaptureSnapshot("03-SettingsDirectory.png"))
    {
        return false;
    }

    // 04: Appearance -> Display
    Panel.ActivatePage(ControlCentrePage::Appearance, true);
    Panel.ActivateAppearanceTab(AppearanceSectionTab::Display);
    if (!CaptureSnapshot("04-AppearanceDisplay.png"))
    {
        return false;
    }

    // 05: Appearance -> Typography (top strip + Typeface & Colors playground)
    Panel.ActivateAppearanceTab(AppearanceSectionTab::Typography);
    Panel.ConfigureScrollOffset(0.0f);
    if (!CaptureSnapshot("05-AppearanceTypography.png"))
    {
        return false;
    }

    // 06: Appearance -> Typography scrolled to TYPE SCALE - DESKTOP
    Panel.ConfigureScrollOffset(540.0f);
    if (!CaptureSnapshot("06-AppearanceTypeScale.png"))
    {
        return false;
    }

    // 07: Appearance -> Theme (Color Scheme 8 tiles + Corner Radius + Accent Color)
    Panel.ActivateAppearanceTab(AppearanceSectionTab::Theme);
    Panel.ConfigureScrollOffset(0.0f);
    if (!CaptureSnapshot("07-AppearanceTheme.png"))
    {
        return false;
    }

    // 08: Appearance -> Theme in Light theme scrolled to Semantic Colors
    Panel.AccessDraftAppearance().Theme  = ThemePreset::Light;
    Panel.AccessDraftAppearance().Accent = AccentPreset::Violet;
    Panel.ConfigureScrollOffset(880.0f);
    if (!CaptureSnapshot("08-AppearanceSemanticAndLight.png", RgbaColor8{ 248, 248, 248, 255 }))
    {
        return false;
    }

    Panel.AccessDraftAppearance() = Panel.QueryAppliedAppearance();

    // 09: Input Settings (with Custom Shortcuts active & capture ring on slot 1)
    Panel.ActivatePage(ControlCentrePage::Input, true);
    Panel.BeginInputShortcutCapture(1u);
    if (!CaptureSnapshot("09-InputSettings.png"))
    {
        return false;
    }
    Panel.AccessDraftInput() = Panel.QueryAppliedInput();

    // 10: Telemetry & Notifications + live FPS indicator + live top-right toast
    Panel.ActivatePage(ControlCentrePage::Notifications, true);
    Panel.AccessDraftNotifications().RateOverlayEnabled = true;
    Panel.ApplyDraftNotificationsImmediate();
    Panel.AccessNotificationStream().Broadcast(
        NotificationSeverity::Notice,
        "FPS telemetry enabled",
        "Top-right refresh rate indicator is now visible across the workspace.",
        6.0f,
        true);
    if (!CaptureSnapshot("10-NotificationsSettings.png"))
    {
        return false;
    }

    // 11: Workspace Settings (Allow detached OS windows + Reset saved layout — zero Content Browser)
    Panel.AccessNotificationStream().ClearPending();
    Panel.ActivatePage(ControlCentrePage::Workspace, true);
    if (!CaptureSnapshot("11-WorkspaceSettings.png"))
    {
        return false;
    }

    // 12: ConfirmationDialogue (Rounded layout: Apply changes?)
    Panel.ActivatePage(ControlCentrePage::Appearance, true);
    Panel.ActivateAppearanceTab(AppearanceSectionTab::Theme);
    {
        ConfirmationSpecification Specification{};
        Specification.Severity     = DialogueSeverity::Notice;
        Specification.Layout       = DialogueLayoutStyle::Rounded;
        Specification.Title        = "Apply changes?";
        Specification.Description  = "Apply modified appearance settings (Theme, Accent, Corner radius) to this .projectspace?";
        Specification.PrimaryLabel = "Apply";
        (void)Panel.OpenConfirmationModal(Specification);
    }
    if (!CaptureSnapshot("12-ConfirmationRounded.png"))
    {
        return false;
    }
    Panel.AccessConfirmationModal().Reset();

    // 13: ConfirmationDialogue (SemanticHeader layout: colored banner + 120px watermark icon)
    {
        ConfirmationSpecification Specification{};
        Specification.Severity     = DialogueSeverity::Error;
        Specification.Layout       = DialogueLayoutStyle::SemanticHeader;
        Specification.Title        = "Reset workspace?";
        Specification.Description  = "Confirming restores default docked splits and clears unsaved shortcut overrides.";
        Specification.PrimaryLabel = "Reset";
        (void)Panel.OpenConfirmationModal(Specification);
    }
    if (!CaptureSnapshot("13-ConfirmationSemanticHeader.png"))
    {
        return false;
    }
    Panel.AccessConfirmationModal().Reset();

    // 14: Dedicated Reusable Controls Showcase (DropdownSelector + TextInputField + CapsuleButton)
    {
        ThemeCatalog ShowcaseTheme{};
        ShowcaseTheme.ConfigureTheme(ThemePreset::Oled);
        ShowcaseTheme.ConfigureAccent(AccentPreset::Blue);
        ControlStyle::ConfigureTheme(
            ShowcaseTheme,
            ThemeCatalog::QuerySemanticColor(0u, 0u),
            ThemeCatalog::QuerySemanticColor(1u, 0u),
            ThemeCatalog::QuerySemanticColor(2u, 0u),
            ThemeCatalog::QuerySemanticColor(3u, 0u));

        TextEditBuffer ProjectNameInput{};
        ProjectNameInput.Begin("FrontierStudio.projectspace");
        ProjectNameInput.SelectionAnchor = 0u;
        ProjectNameInput.Caret           = 14u;

        TextEditBuffer SearchInput{};
        SearchInput.Begin("Filter shortcuts...");
        SearchInput.Active = false;

        uint32_t SelectedProfile = 1u;
        uint32_t ChosenProfile   = 1u;

        Io.DisplaySize = ImVec2(static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();

        ImFont*    RegularFont = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular);
        PixelSpace Surface(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f, RegularFont);
        const ControlColorSet& Colors = ControlStyle::Colors();

        Surface.FillRectangle({ 0.0f, 0.0f, 1280.0f, 800.0f }, Colors.Field.ToRgba8(), 0.0f);
        const Rectangle2D Sheet{ 64.0f, 48.0f, 1152.0f, 704.0f };
        Surface.FillRectangle(Sheet, Colors.Panel.ToRgba8(), 28.0f);
        ControlStyle::DrawRoundedOutline(Surface, Sheet, Colors.Stroke, 28.0f);

        Surface.Text({ 104.0f, 84.0f }, 24.0f, Colors.Text.ToRgba8(), "Reusable Studio Controls");
        Surface.Text(
            { 104.0f, 116.0f },
            14.0f,
            Colors.TextDim.ToRgba8(),
            "Standalone Engine/Components/Controls verification — TextInputField, DropdownSelector, CapsuleButton");

        ControlPointer IdlePointer{};
        Surface.Text({ 104.0f, 174.0f }, 13.0f, Colors.TextDim.ToRgba8(), "Focused TextInputField (active selection)");
        (void)TextInputField::Draw(Surface, { 104.0f, 200.0f, 460.0f, 40.0f }, ProjectNameInput, IdlePointer, 13.0f, 1.0f);

        Surface.Text({ 104.0f, 266.0f }, 13.0f, Colors.TextDim.ToRgba8(), "Inactive TextInputField");
        (void)TextInputField::Draw(Surface, { 104.0f, 292.0f, 460.0f, 40.0f }, SearchInput, IdlePointer, 13.0f, 1.0f);

        Surface.Text({ 640.0f, 174.0f }, 13.0f, Colors.TextDim.ToRgba8(), "DropdownSelector (expanded options)");
        constexpr const char* Options[3] = { "Editor Default", "Custom Studio Profile", "Laptop Compact" };
        const Rectangle2D     DropAnchor{ 640.0f, 200.0f, 440.0f, 40.0f };
        (void)DropdownSelector::DrawButton(Surface, DropAnchor, Options[SelectedProfile], true, IdlePointer);
        (void)DropdownSelector::DrawOptionSheet(Surface, DropAnchor, Options, 3u, SelectedProfile, IdlePointer, ChosenProfile);

        Surface.Text({ 104.0f, 384.0f }, 13.0f, Colors.TextDim.ToRgba8(), "CapsuleButton Tones");
        (void)CapsuleButton::Draw(
            Surface,
            { 104.0f, 412.0f, 128.0f, 40.0f },
            ButtonDescriptor{ "Apply", ButtonTone::Primary, 14.0f, 24.0f, false, {} },
            IdlePointer);
        (void)CapsuleButton::Draw(
            Surface,
            { 248.0f, 412.0f, 136.0f, 40.0f },
            ButtonDescriptor{ "Discard", ButtonTone::Secondary, 14.0f, 24.0f, false, {} },
            IdlePointer);
        (void)CapsuleButton::Draw(
            Surface,
            { 400.0f, 412.0f, 136.0f, 40.0f },
            ButtonDescriptor{ "Delete", ButtonTone::Danger, 14.0f, 24.0f, false, {} },
            IdlePointer);

        ImGui::Render();
        SoftwareRasterCanvas Canvas(CanvasWidth, CanvasHeight, RgbaColor8{ 0, 0, 0, 255 });
        Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        if (!VerifyAssertion(Canvas.WritePngFile(OutputDirectory / "14-ReusableControlsKit.png"), "14-ReusableControlsKit.png"))
        {
            return false;
        }
    }

    Dock.Finalize();
    ImGui::DestroyContext(GuiSession);
    return true;
}

} // namespace

int main()
{
    if (!ValidateProjectSpaceContainerAndPascalCaseSchema())
    {
        return 1;
    }
    if (!ValidateInputShortcutConflictDetection())
    {
        return 1;
    }

    const auto OutputDirectory = ResolveRepositoryRootPath() / "Exhibits/Images/ControlCentre";
    if (!RenderSnapshotSuite(OutputDirectory))
    {
        return 1;
    }

    std::printf("[ControlCentreValidation] All Control Centre, Reusable Controls, and PascalCase .projectspace checks passed.\n");
    return 0;
}
