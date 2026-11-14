/*============================================================================================================================================
                                                         CONTROLCENTREVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation and 16-image CPU visual snapshot suite for ControlCentrePanel, reusable Controls, Tooltips, and .projectspace persistence

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/TypographyRepository.h"
#include "Engine/Components/ControlCentre/ControlCentrePanel.h"
#include "Engine/Components/Controls/ControlTooltip.h"
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
using Frontier::ColorRgba;
using Frontier::ConfirmationSpecification;
using Frontier::ControlCentrePage;
using Frontier::ControlCentrePanel;
using Frontier::ControlColorSet;
using Frontier::ControlContact;
using Frontier::ControlIcon;
using Frontier::ControlPointer;
using Frontier::DialogueLayoutStyle;
using Frontier::DialogueSeverity;
using Frontier::InputPreferences;
using Frontier::NotificationPreferences;
using Frontier::NotificationSeverity;
using Frontier::OptionalTooltip;
using Frontier::PixelSpace;
using Frontier::Point2D;
using Frontier::ProjectSpaceRepository;
using Frontier::ProjectSpaceDocument;
using Frontier::Rectangle2D;
using Frontier::ShortcutChord;
using Frontier::ShortcutKey;
using Frontier::TextEditBuffer;
using Frontier::ThemeRepository;
using Frontier::ThemePreset;
using Frontier::TooltipDescriptor;
using Frontier::TooltipDirection;
using Frontier::TooltipInteractionResult;
using Frontier::TooltipLayoutVariant;
using Frontier::TooltipMetricToken;
using Frontier::TooltipSeverity;
using Frontier::TooltipSuggestion;
using Frontier::TooltipSuggestionTone;
using Frontier::TooltipSurfaceMode;
using Frontier::TooltipTailAlignment;
using Frontier::TooltipTailProfile;
using Frontier::TypographyRepository;
using Frontier::VectorIconRepository;
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

namespace CapsuleButton           = Frontier::CapsuleButton;
namespace ControlAppearance       = Frontier::ControlAppearance;
namespace ControlTooltip          = Frontier::ControlTooltip;
namespace DropdownSelector        = Frontier::DropdownSelector;
namespace ExclusiveSelector       = Frontier::ExclusiveSelector;
namespace InputSettingsRepository = Frontier::InputSettingsRepository;
namespace ScalarSlider            = Frontier::ScalarSlider;
namespace TextInputField          = Frontier::TextInputField;

//------------------------------------------------------------------------------------------------------------------------
//                                                 VALIDATION ASSERTIONS
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
            !ProjectSpaceRepository::ValidateContainerPath("/tmp/BadConfig.toml", Diagnostic),
            "ProjectSpaceRepository rejects .toml container paths"))
    {
        return false;
    }

    const std::filesystem::path TemporaryRepository = std::filesystem::temp_directory_path() / "FrontierValidation.projectspace";
    std::error_code             IoFailure{};
    std::filesystem::remove(TemporaryRepository, IoFailure);

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

    if (!VerifyAssertion(ProjectSpaceRepository::Save(TemporaryRepository, OutboundDocument, Diagnostic), "Save .projectspace container"))
    {
        return false;
    }

    {
        std::ifstream     Stream(TemporaryRepository);
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
    if (!VerifyAssertion(ProjectSpaceRepository::Load(TemporaryRepository, InboundDocument, Diagnostic), "Load .projectspace container"))
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

    std::filesystem::remove(TemporaryRepository, IoFailure);
    return true;
}

bool ValidateInputShortcutConflictDetection()
{
    InputPreferences DuplicatePreferences{};
    DuplicatePreferences.CustomShortcutsEnabled = true;
    DuplicatePreferences.Shortcuts[1]           = DuplicatePreferences.Shortcuts[0];

    std::string Diagnostic{};
    if (!VerifyAssertion(
            !InputSettingsRepository::ValidatePreferences(DuplicatePreferences, Diagnostic) && !Diagnostic.empty(),
            "Duplicate shortcut chords rejected with diagnostic"))
    {
        return false;
    }

    InputPreferences BareLetterPreferences{};
    BareLetterPreferences.CustomShortcutsEnabled = true;
    BareLetterPreferences.Shortcuts[1]           = ShortcutChord{ ShortcutKey::K, false, false, false, false };
    if (!VerifyAssertion(
            !InputSettingsRepository::ValidatePreferences(BareLetterPreferences, Diagnostic),
            "Modifierless printable letter shortcut rejected"))
    {
        return false;
    }

    InputPreferences ValidPreferences{};
    ValidPreferences.CustomShortcutsEnabled = true;
    ValidPreferences.Shortcuts[1]           = ShortcutChord{ ShortcutKey::K, true, false, true, false };
    if (!VerifyAssertion(
            InputSettingsRepository::ValidatePreferences(ValidPreferences, Diagnostic) &&
            InputSettingsRepository::FormatChordLabel(ValidPreferences.Shortcuts[1]) == "Ctrl + Shift + K",
            "Ctrl + Shift + K chord validates and formats cleanly"))
    {
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            VISUAL SNAPSHOT SUITE GENERATION
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
    TypographyRepository Typefaces{};
    if (!VerifyAssertion(
            Typefaces.LoadRepository(RepositoryRoot / "EngineContent/FontsArchives") == 5u,
            "Load FontRepository.FontSpace (zero .toml)"))
    {
        ImGui::DestroyContext(GuiSession);
        return false;
    }
    TypographyRepository::AssignCurrent(&Typefaces);
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
        ConfigureImGuiWorkspaceTheme(Dock.ActiveTabMetrics(), Panel.QueryThemeRepository());
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
        ThemeRepository ShowcaseTheme{};
        ShowcaseTheme.ConfigureTheme(ThemePreset::Oled);
        ShowcaseTheme.ConfigureAccent(AccentPreset::Blue);
        ControlAppearance::ConfigureTheme(
            ShowcaseTheme,
            ThemeRepository::QuerySemanticColor(0u, 0u),
            ThemeRepository::QuerySemanticColor(1u, 0u),
            ThemeRepository::QuerySemanticColor(2u, 0u),
            ThemeRepository::QuerySemanticColor(3u, 0u));

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
        const ControlColorSet& Colors = ControlAppearance::Colors();

        Surface.FillRectangle({ 0.0f, 0.0f, 1280.0f, 800.0f }, Colors.Field.ToRgba8(), 0.0f);
        const Rectangle2D Sheet{ 64.0f, 48.0f, 1152.0f, 704.0f };
        Surface.FillRectangle(Sheet, Colors.Panel.ToRgba8(), 28.0f);
        ControlAppearance::ConstructRoundedOutline(Surface, Sheet, Colors.Stroke, 28.0f);

        Surface.Text({ 104.0f, 84.0f }, 24.0f, Colors.Text.ToRgba8(), "Reusable Studio Controls");
        Surface.Text(
            { 104.0f, 116.0f },
            14.0f,
            Colors.TextDim.ToRgba8(),
            "Standalone Engine/Components/Controls verification — TextInputField, DropdownSelector, CapsuleButton");

        ControlPointer IdlePointer{};
        Surface.Text({ 104.0f, 174.0f }, 13.0f, Colors.TextDim.ToRgba8(), "Focused TextInputField (active selection)");
        (void)TextInputField::Construct(Surface, { 104.0f, 200.0f, 460.0f, 40.0f }, ProjectNameInput, IdlePointer, 13.0f, 1.0f);

        Surface.Text({ 104.0f, 266.0f }, 13.0f, Colors.TextDim.ToRgba8(), "Inactive TextInputField");
        (void)TextInputField::Construct(Surface, { 104.0f, 292.0f, 460.0f, 40.0f }, SearchInput, IdlePointer, 13.0f, 1.0f);

        Surface.Text({ 640.0f, 174.0f }, 13.0f, Colors.TextDim.ToRgba8(), "DropdownSelector (expanded options)");
        constexpr const char* Options[3] = { "Editor Default", "Custom Studio Profile", "Laptop Compact" };
        const Rectangle2D     DropAnchor{ 640.0f, 200.0f, 440.0f, 40.0f };
        (void)DropdownSelector::ConstructButton(Surface, DropAnchor, Options[SelectedProfile], true, IdlePointer);
        (void)DropdownSelector::ConstructOptionSheet(Surface, DropAnchor, Options, 3u, SelectedProfile, IdlePointer, ChosenProfile);

        Surface.Text({ 104.0f, 384.0f }, 13.0f, Colors.TextDim.ToRgba8(), "CapsuleButton Tones");
        (void)CapsuleButton::Construct(
            Surface,
            { 104.0f, 412.0f, 128.0f, 40.0f },
            ButtonDescriptor{ "Apply", ButtonTone::Primary, 14.0f, 24.0f, false, {} },
            IdlePointer);
        (void)CapsuleButton::Construct(
            Surface,
            { 248.0f, 412.0f, 136.0f, 40.0f },
            ButtonDescriptor{ "Discard", ButtonTone::Secondary, 14.0f, 24.0f, false, {} },
            IdlePointer);
        (void)CapsuleButton::Construct(
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

    // 15: Light/Dark Base Tooltips + 4-Direction Speech Tooltips + Hovered Control OptionalTooltip
    {
        ThemeRepository ShowcaseTheme{};
        ShowcaseTheme.ConfigureTheme(ThemePreset::Dark);
        ShowcaseTheme.ConfigureAccent(AccentPreset::Blue);
        ControlAppearance::ConfigureTheme(ShowcaseTheme);

        Io.DisplaySize = ImVec2(static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();

        ImFont*    RegularFont = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular);
        PixelSpace Surface(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f, RegularFont);

        // Left half: Neutral light-gray stage showcasing Light & Dark base tooltips (e6ee3086...)
        Surface.FillRectangle({ 0.0f, 0.0f, 620.0f, 800.0f }, ColorRgba{ 212u, 214u, 220u, 255u }, 0.0f);
        Surface.Text({ 36.0f, 28.0f }, 17.0f, ColorRgba{ 24u, 26u, 32u, 255u }, "Light & Dark Base Tooltips + Control Attachment");

        ControlPointer IdlePointer{};

        auto ConstructHintTrigger = [&](Rectangle2D TriggerBounds, bool DarkTrigger)
        {
            const ColorRgba Fill = DarkTrigger ? ColorRgba{ 18u, 19u, 23u, 255u } : ColorRgba{ 255u, 255u, 255u, 255u };
            const ColorRgba Ink  = DarkTrigger ? ColorRgba{ 245u, 246u, 249u, 255u } : ColorRgba{ 20u, 22u, 26u, 255u };
            Surface.FillRectangle(TriggerBounds, Fill, 14.0f);
            VectorIconRepository::ConstructControl(
                Surface,
                ControlIcon::LightbulbHint,
                { TriggerBounds.X + 13.0f, TriggerBounds.Y + 13.0f, 22.0f, Ink, 2.0f });
        };

        const Rectangle2D LightTrigger{ 54.0f, 254.0f, 48.0f, 48.0f };
        ConstructHintTrigger(LightTrigger, false);

        TooltipDescriptor LightBase{};
        LightBase.Title          = "Tooltip";
        LightBase.BodyText       = "Try connecting to another server. In case of a repeated error, please wait or inspect session network diagnostics.";
        LightBase.Direction      = TooltipDirection::Above;
        LightBase.TailProfile    = TooltipTailProfile::RoundedPointer;
        LightBase.TailAlignment  = TooltipTailAlignment::Leading;
        LightBase.SurfaceMode    = TooltipSurfaceMode::LightSurface;
        LightBase.PreferredWidth = 258.0f;
        LightBase.CornerRadius   = 22.0f;
        (void)ControlTooltip::ConstructAtBodyBounds(
            Surface,
            { 36.0f, 92.0f, 258.0f, 138.0f },
            LightTrigger.Center(),
            TooltipDirection::Above,
            LightBase,
            IdlePointer);

        const Rectangle2D DarkTrigger{ 342.0f, 254.0f, 48.0f, 48.0f };
        ConstructHintTrigger(DarkTrigger, true);

        TooltipDescriptor DarkBase = LightBase;
        DarkBase.SurfaceMode       = TooltipSurfaceMode::DarkSurface;
        (void)ControlTooltip::ConstructAtBodyBounds(
            Surface,
            { 324.0f, 92.0f, 258.0f, 138.0f },
            DarkTrigger.Center(),
            TooltipDirection::Above,
            DarkBase,
            IdlePointer);

        // Lower-left dark panel demonstrating OptionalTooltip attached directly to a Studio Control
        const Rectangle2D ControlPanelRect{ 32.0f, 348.0f, 556.0f, 420.0f };
        Surface.FillRectangle(ControlPanelRect, ColorRgba{ 24u, 26u, 31u, 255u }, 22.0f);
        Surface.StrokeRectangle(ControlPanelRect, ColorRgba{ 255u, 255u, 255u, 24u }, 22.0f, 1.0f);
        Surface.Text({ 58.0f, 374.0f }, 15.0f, ColorRgba{ 240u, 242u, 246u, 255u }, "Studio Controls with OptionalTooltip");
        Surface.Text({ 58.0f, 398.0f }, 12.5f, ColorRgba{ 148u, 152u, 164u, 255u }, "Hovering any control automatically queues its directional tooltip");

        const Rectangle2D HoveredBtnBounds{ 58.0f, 568.0f, 178.0f, 40.0f };
        ControlPointer    HoverButtonPointer{};
        HoverButtonPointer.X       = HoveredBtnBounds.Center().X;
        HoverButtonPointer.Y       = HoveredBtnBounds.Center().Y;
        HoverButtonPointer.Enabled = true;

        ButtonDescriptor BakeButton{};
        BakeButton.Label   = "Pack UV Charts";
        BakeButton.Tone    = ButtonTone::Primary;
        BakeButton.Tooltip = OptionalTooltip::FromTitled(
            "Importance UV Packer",
            "Packs selected chart islands into UDIM 1001 weighted by world-space surface area.",
            TooltipDirection::Above,
            TooltipSeverity::Informational,
            ControlIcon::LayersSlabs);
        const ControlContact BtnHit = CapsuleButton::Construct(Surface, HoveredBtnBounds, BakeButton, HoverButtonPointer);
        if (!VerifyAssertion(BtnHit.Hovered, "Hovered CapsuleButton registers hover contact"))
        {
            return false;
        }

        Surface.Text({ 278.0f, 578.0f }, 13.0f, ColorRgba{ 196u, 199u, 208u, 255u }, "Snap Seam Borders");
        (void)ExclusiveSelector::Construct(
            Surface,
            422.0f,
            575.0f,
            true,
            IdlePointer,
            1.0f,
            OptionalTooltip::FromText("Aligns UV boundary vertices to pixel centers."));

        float PreviewTexelDensity = 512.0f;
        Surface.Text({ 58.0f, 654.0f }, 13.0f, ColorRgba{ 196u, 199u, 208u, 255u }, "Texel Density");
        (void)ScalarSlider::ConstructWithReadout(
            Surface,
            { 176.0f, 642.0f, 380.0f, 40.0f },
            "512",
            "px/m",
            128.0f,
            2048.0f,
            PreviewTexelDensity,
            false,
            IdlePointer,
            PreviewTexelDensity,
            1.0f,
            OptionalTooltip::FromText("Target texel resolution per world meter."));

        const bool ConsumedHoverTooltip = ControlTooltip::PresentHoveredControlTooltip(Surface, ControlPanelRect, HoverButtonPointer);
        if (!VerifyAssertion(ConsumedHoverTooltip, "PresentHoveredControlTooltip renders queued control tooltip"))
        {
            return false;
        }

        // Right half: Crisp light backdrop showcasing 4-direction speech tooltips & launch buttons (b705d179...)
        Surface.FillRectangle({ 620.0f, 0.0f, 660.0f, 800.0f }, ColorRgba{ 244u, 247u, 251u, 255u }, 0.0f);
        Surface.Text({ 654.0f, 28.0f }, 17.0f, ColorRgba{ 18u, 22u, 30u, 255u }, "Directional Speech Tooltips (Above · Left · Right · Below)");

        auto ConstructHelpIndicator = [&](Point2D Center) -> Rectangle2D
        {
            Surface.FillCircle(Center, 14.0f, ColorRgba{ 52u, 130u, 246u, 255u });
            const Point2D QSize = Surface.MeasureText(14.5f, "?");
            Surface.Text({ Center.X - QSize.X * 0.5f, Center.Y - QSize.Y * 0.5f }, 14.5f, ColorRgba{ 255u, 255u, 255u, 255u }, "?");
            return { Center.X - 14.0f, Center.Y - 14.0f, 28.0f, 28.0f };
        };

        // (a) Above anchor (tail down) + "Learn More [ExternalOpen]"
        const TooltipSuggestion LearnMoreSuggestion[1] = {
            { "Learn More", ControlIcon::Count, ControlIcon::ExternalOpen, TooltipSuggestionTone::Neutral },
        };
        const Rectangle2D TopAnchor = ConstructHelpIndicator({ 828.0f, 270.0f });
        TooltipDescriptor AboveSpec{};
        AboveSpec.BodyText       = "Variables preserve authored transform parameters across simulation steps.";
        AboveSpec.Direction      = TooltipDirection::Above;
        AboveSpec.SurfaceMode    = TooltipSurfaceMode::DarkSurface;
        AboveSpec.PreferredWidth = 286.0f;
        AboveSpec.CornerRadius   = 18.0f;
        AboveSpec.Suggestions    = LearnMoreSuggestion;
        (void)ControlTooltip::ConstructAnchored(Surface, TopAnchor, { 632.0f, 60.0f, 636.0f, 720.0f }, AboveSpec, IdlePointer);

        // (b) Left of anchor (tail right)
        const Rectangle2D RightEdgeAnchor = ConstructHelpIndicator({ 1228.0f, 362.0f });
        TooltipDescriptor LeftSpec{};
        LeftSpec.BodyText       = "This solver requires two additional boundary constraints to lock rotation.";
        LeftSpec.Direction      = TooltipDirection::Left;
        LeftSpec.SurfaceMode    = TooltipSurfaceMode::DarkSurface;
        LeftSpec.PreferredWidth = 278.0f;
        LeftSpec.CornerRadius   = 18.0f;
        (void)ControlTooltip::ConstructAnchored(Surface, RightEdgeAnchor, { 632.0f, 60.0f, 636.0f, 720.0f }, LeftSpec, IdlePointer);

        // (c) Right of anchor (tail left) + "Open Docs [ExternalOpen]"
        const TooltipSuggestion OpenDocsSuggestion[1] = {
            { "Open Docs", ControlIcon::Count, ControlIcon::ExternalOpen, TooltipSuggestionTone::Neutral },
        };
        const Rectangle2D LeftEdgeAnchor = ConstructHelpIndicator({ 666.0f, 542.0f });
        TooltipDescriptor RightSpec{};
        RightSpec.BodyText       = "Chart seam pinning preserves authored border scale during LSCM relaxation.";
        RightSpec.Direction      = TooltipDirection::Right;
        RightSpec.SurfaceMode    = TooltipSurfaceMode::DarkSurface;
        RightSpec.PreferredWidth = 286.0f;
        RightSpec.CornerRadius   = 18.0f;
        RightSpec.Suggestions    = OpenDocsSuggestion;
        (void)ControlTooltip::ConstructAnchored(Surface, LeftEdgeAnchor, { 632.0f, 60.0f, 636.0f, 720.0f }, RightSpec, IdlePointer);

        // (d) Below anchor (tail up) — also verifies Automatic flip near top viewport edge
        const Rectangle2D UpperClipAnchor{ 1090.0f, 68.0f, 28.0f, 28.0f };
        const TooltipDirection FlippedDir = ControlTooltip::ResolvePlacementDirection(
            UpperClipAnchor,
            { 632.0f, 60.0f, 636.0f, 720.0f },
            { 240.0f, 90.0f },
            TooltipDirection::Automatic);
        if (!VerifyAssertion(FlippedDir == TooltipDirection::Below, "Automatic tooltip flips Below when anchor nears top viewport edge"))
        {
            return false;
        }

        const Rectangle2D BottomSectionAnchor = ConstructHelpIndicator({ 1098.0f, 646.0f });
        TooltipDescriptor BelowSpec{};
        BelowSpec.BodyText       = "Shift-drag adjusts scalar precision in 0.001 increments.";
        BelowSpec.Direction      = TooltipDirection::Below;
        BelowSpec.SurfaceMode    = TooltipSurfaceMode::DarkSurface;
        BelowSpec.PreferredWidth = 264.0f;
        BelowSpec.CornerRadius   = 18.0f;
        (void)ControlTooltip::ConstructAnchored(Surface, BottomSectionAnchor, { 632.0f, 60.0f, 636.0f, 720.0f }, BelowSpec, IdlePointer);

        ImGui::Render();
        SoftwareRasterCanvas Canvas(CanvasWidth, CanvasHeight, RgbaColor8{ 18, 18, 22, 255 });
        Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        if (!VerifyAssertion(
                Canvas.WritePngFile(OutputDirectory / "15-TooltipsLightDarkAndDirectional.png"),
                "15-TooltipsLightDarkAndDirectional.png"))
        {
            return false;
        }
    }

    // 16: Split-Header Diagnostic Tooltips (Informational · Caution · Warning UV Overlap · Error Degenerate Import)
    {
        ThemeRepository ShowcaseTheme{};
        ShowcaseTheme.ConfigureTheme(ThemePreset::Dark);
        ShowcaseTheme.ConfigureAccent(AccentPreset::Blue);
        ControlAppearance::ConfigureTheme(ShowcaseTheme);

        Io.DisplaySize = ImVec2(static_cast<float>(CanvasWidth), static_cast<float>(CanvasHeight));
        Io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();

        ImFont*    RegularFont = Typefaces.QueryFont(0u, Frontier::FontWeightClass::Regular);
        PixelSpace Surface(ImGui::GetForegroundDrawList(), { 0.0f, 0.0f }, 1.0f, RegularFont);

        Surface.FillRectangle({ 0.0f, 0.0f, 1280.0f, 800.0f }, ColorRgba{ 14u, 15u, 18u, 255u }, 0.0f);
        Surface.Text({ 48.0f, 28.0f }, 20.0f, ColorRgba{ 242u, 244u, 248u, 255u }, "Diagnostic & Informational Split-Header Tooltips");
        Surface.Text(
            { 48.0f, 56.0f },
            13.0f,
            ColorRgba{ 142u, 146u, 158u, 255u },
            "Informational, Caution, Warning (UV Overlap: Fix / Ignore), and Error (Degenerate Facets: Fix / Reimport / Ignore)");

        ControlPointer IdlePointer{};

        // Quadrant 1 (Top-Left): Informational SplitHeader with top speech pointer (8226e835... style)
        const Rectangle2D InfoRegion{ 44.0f, 92.0f, 576.0f, 326.0f };
        Surface.FillRectangle(InfoRegion, ColorRgba{ 250u, 250u, 252u, 255u }, 20.0f);
        Surface.Text({ 76.0f, 118.0f }, 16.0f, ColorRgba{ 204u, 58u, 58u, 255u }, "\"Precision geometry begins with clean parameterization.\"");
        Surface.StrokeLine({ 152.0f, 142.0f }, { 236.0f, 142.0f }, ColorRgba{ 204u, 58u, 58u, 255u }, 1.5f);

        const TooltipMetricToken InfoMetrics[3] = {
            { "26px", ControlIcon::SparklesAntiAliasing, {}, false },
            { "0px", ControlIcon::SlidersQuality, {}, false },
            { "FF595A", ControlIcon::Count, ColorRgba{ 255u, 89u, 90u, 255u }, true },
        };
        const TooltipSuggestion InfoSuggestions[2] = {
            { "Inspect Style", ControlIcon::EyeVisible, ControlIcon::Count, TooltipSuggestionTone::Neutral },
            { "Ignore", ControlIcon::Count, ControlIcon::Count, TooltipSuggestionTone::SubtleDismiss },
        };
        TooltipDescriptor InfoTooltip{};
        InfoTooltip.Title          = "Nocturno Display";
        InfoTooltip.HeaderTag      = "Bold";
        InfoTooltip.BodyText       = "Active display specimen carries custom tracking and optical kerning.";
        InfoTooltip.Direction      = TooltipDirection::Below;
        InfoTooltip.Layout         = TooltipLayoutVariant::SplitHeader;
        InfoTooltip.Severity       = TooltipSeverity::Informational;
        InfoTooltip.SurfaceMode    = TooltipSurfaceMode::DarkSurface;
        InfoTooltip.PreferredWidth = 344.0f;
        InfoTooltip.CornerRadius   = 18.0f;
        InfoTooltip.MetricTokens   = InfoMetrics;
        InfoTooltip.Suggestions    = InfoSuggestions;
        (void)ControlTooltip::ConstructAtBodyBounds(
            Surface,
            { 118.0f, 162.0f, 356.0f, 214.0f },
            { 194.0f, 144.0f },
            TooltipDirection::Below,
            InfoTooltip,
            IdlePointer);

        // Quadrant 2 (Top-Right): Warning SplitHeader — UV Chart Overlap (Fix / Ignore)
        const Rectangle2D UvRegion{ 648.0f, 92.0f, 588.0f, 326.0f };
        Surface.FillRectangle(UvRegion, ColorRgba{ 22u, 24u, 29u, 255u }, 20.0f);
        Surface.StrokeRectangle(UvRegion, ColorRgba{ 255u, 255u, 255u, 20u }, 20.0f, 1.0f);
        const Rectangle2D UvAnchor{ 906.0f, 368.0f, 72.0f, 28.0f };
        Surface.FillRectangle(UvAnchor, ColorRgba{ 245u, 158u, 11u, 44u }, 8.0f);
        Surface.StrokeRectangle(UvAnchor, ColorRgba{ 245u, 158u, 11u, 190u }, 8.0f, 1.0f);
        Surface.TextCentered(UvAnchor, 11.5f, ColorRgba{ 252u, 211u, 77u, 255u }, "UDIM 1001");

        const TooltipMetricToken UvMetrics[2] = {
            { "3 islands", ControlIcon::LayersSlabs, {}, false },
            { "4.8% overlap", ControlIcon::Count, ColorRgba{ 245u, 158u, 11u, 255u }, true },
        };
        const TooltipSuggestion UvSuggestions[2] = {
            { "Fix Overlap", ControlIcon::WrenchRepair, ControlIcon::Count, TooltipSuggestionTone::SemanticPrimary },
            { "Ignore", ControlIcon::CloseCross, ControlIcon::Count, TooltipSuggestionTone::SubtleDismiss },
        };
        TooltipDescriptor UvWarning{};
        UvWarning.Title          = "UV Chart Overlap";
        UvWarning.HeaderTag      = "Warning";
        UvWarning.BodyText       = "Overlapping parameter islands detected in UDIM 1001. Baking lightmaps or curvature will overwrite shared texels.";
        UvWarning.Direction      = TooltipDirection::Above;
        UvWarning.Layout         = TooltipLayoutVariant::SplitHeader;
        UvWarning.Severity       = TooltipSeverity::Warning;
        UvWarning.SurfaceMode    = TooltipSurfaceMode::DarkSurface;
        UvWarning.PreferredWidth = 376.0f;
        UvWarning.CornerRadius   = 18.0f;
        UvWarning.MetricTokens   = UvMetrics;
        UvWarning.Suggestions    = UvSuggestions;
        const TooltipInteractionResult UvLayoutResult =
            ControlTooltip::ConstructAnchored(Surface, UvAnchor, UvRegion, UvWarning, IdlePointer);

        // Quadrant 3 (Bottom-Left): Error SplitHeader — Degenerate Imported Geometry (Fix / Reimport / Ignore)
        const Rectangle2D ImportRegion{ 44.0f, 442.0f, 576.0f, 326.0f };
        Surface.FillRectangle(ImportRegion, ColorRgba{ 22u, 24u, 29u, 255u }, 20.0f);
        Surface.StrokeRectangle(ImportRegion, ColorRgba{ 255u, 255u, 255u, 20u }, 20.0f, 1.0f);
        const Rectangle2D ImportAnchor{ 262.0f, 718.0f, 140.0f, 28.0f };
        Surface.FillRectangle(ImportAnchor, ColorRgba{ 239u, 68u, 68u, 44u }, 8.0f);
        Surface.StrokeRectangle(ImportAnchor, ColorRgba{ 239u, 68u, 68u, 190u }, 8.0f, 1.0f);
        Surface.TextCentered(ImportAnchor, 11.5f, ColorRgba{ 252u, 165u, 165u, 255u }, "WheelArch.facet");

        const TooltipMetricToken DegenerateMetrics[2] = {
            { "14 zero-area facets", ControlIcon::CubeShape, {}, false },
            { "3 open seams", ControlIcon::Count, ColorRgba{ 239u, 68u, 68u, 255u }, true },
        };
        const TooltipSuggestion DegenerateSuggestions[3] = {
            { "Fix", ControlIcon::WrenchRepair, ControlIcon::Count, TooltipSuggestionTone::SemanticPrimary },
            { "Reimport", ControlIcon::RefreshCycle, ControlIcon::Count, TooltipSuggestionTone::Neutral },
            { "Ignore", ControlIcon::CloseCross, ControlIcon::Count, TooltipSuggestionTone::SubtleDismiss },
        };
        TooltipDescriptor ErrorTooltip{};
        ErrorTooltip.Title          = "Degenerate Facets";
        ErrorTooltip.HeaderTag      = "Error";
        ErrorTooltip.BodyText       = "Imported surface contains collapsed zero-area facets along the fender seam that block watertight boolean evaluation.";
        ErrorTooltip.Direction      = TooltipDirection::Above;
        ErrorTooltip.Layout         = TooltipLayoutVariant::SplitHeader;
        ErrorTooltip.Severity       = TooltipSeverity::Error;
        ErrorTooltip.SurfaceMode    = TooltipSurfaceMode::DarkSurface;
        ErrorTooltip.PreferredWidth = 408.0f;
        ErrorTooltip.CornerRadius   = 18.0f;
        ErrorTooltip.MetricTokens   = DegenerateMetrics;
        ErrorTooltip.Suggestions    = DegenerateSuggestions;
        const TooltipInteractionResult ErrorLayoutResult =
            ControlTooltip::ConstructAnchored(Surface, ImportAnchor, ImportRegion, ErrorTooltip, IdlePointer);

        // Verify deterministic click routing across Fix (0), Reimport (1), and Ignore (2)
        {
            ControlPointer ClickReimport{};
            ClickReimport.X        = ErrorLayoutResult.BodyBounds.Center().X;
            ClickReimport.Y        = ErrorLayoutResult.BodyBounds.Bottom() - 28.0f;
            ClickReimport.Released = true;
            ClickReimport.Enabled  = true;

            const TooltipInteractionResult ClickTest = ControlTooltip::ConstructAtBodyBounds(
                Surface,
                ErrorLayoutResult.BodyBounds,
                ImportAnchor.Center(),
                ErrorLayoutResult.ResolvedDirection,
                ErrorTooltip,
                ClickReimport);
            if (!VerifyAssertion(
                    ClickTest.ClickedSuggestionIndex == 1 && UvLayoutResult.BodyBounds.Width >= 360.0f,
                    "Diagnostic tooltip routes suggestion button clicks (Reimport == index 1)"))
            {
                return false;
            }
        }

        // Quadrant 4 (Bottom-Right): Caution SplitHeader — Non-Uniform Scale Skew
        const Rectangle2D CautionRegion{ 648.0f, 442.0f, 588.0f, 326.0f };
        Surface.FillRectangle(CautionRegion, ColorRgba{ 22u, 24u, 29u, 255u }, 20.0f);
        Surface.StrokeRectangle(CautionRegion, ColorRgba{ 255u, 255u, 255u, 20u }, 20.0f, 1.0f);
        const Rectangle2D CautionAnchor{ 882.0f, 466.0f, 120.0f, 28.0f };
        Surface.FillRectangle(CautionAnchor, ColorRgba{ 234u, 179u, 8u, 44u }, 8.0f);
        Surface.StrokeRectangle(CautionAnchor, ColorRgba{ 234u, 179u, 8u, 190u }, 8.0f, 1.0f);
        Surface.TextCentered(CautionAnchor, 11.5f, ColorRgba{ 253u, 224u, 71u, 255u }, "Scale 1.0 · 2.4 · 0.6");

        const TooltipSuggestion CautionSuggestions[2] = {
            { "Normalize Scale", ControlIcon::WrenchRepair, ControlIcon::Count, TooltipSuggestionTone::SemanticPrimary },
            { "Ignore", ControlIcon::Count, ControlIcon::Count, TooltipSuggestionTone::SubtleDismiss },
        };
        TooltipDescriptor CautionTooltip{};
        CautionTooltip.Title          = "Non-Uniform Scale";
        CautionTooltip.HeaderTag      = "Caution";
        CautionTooltip.BodyText       = "Enclosing transform stretches Z by 0.6x. Rolling-ball fillet radii will appear elliptical until scale is normalized.";
        CautionTooltip.Direction      = TooltipDirection::Below;
        CautionTooltip.Layout         = TooltipLayoutVariant::SplitHeader;
        CautionTooltip.Severity       = TooltipSeverity::Caution;
        CautionTooltip.SurfaceMode    = TooltipSurfaceMode::DarkSurface;
        CautionTooltip.PreferredWidth = 384.0f;
        CautionTooltip.CornerRadius   = 18.0f;
        CautionTooltip.Suggestions    = CautionSuggestions;
        (void)ControlTooltip::ConstructAnchored(Surface, CautionAnchor, CautionRegion, CautionTooltip, IdlePointer);

        ImGui::Render();
        SoftwareRasterCanvas Canvas(CanvasWidth, CanvasHeight, RgbaColor8{ 14, 15, 18, 255 });
        Canvas.RasterizeImGuiBatches(ImGui::GetDrawData());
        if (!VerifyAssertion(
                Canvas.WritePngFile(OutputDirectory / "16-DiagnosticSeverityTooltips.png"),
                "16-DiagnosticSeverityTooltips.png"))
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
