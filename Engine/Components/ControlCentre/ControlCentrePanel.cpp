/*============================================================================================================================================
                                                            CONTROLCENTREPANEL.CPP
============================================================================================================================================*/
// 🧩 Top-notch pull-down Control Centre, Appearance/Input/Notifications/Workspace settings, toast overlay, and modal dialogues

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/ControlCentre/ControlCentrePanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr FontWeightClass TypographyChipOrder[9] = {
    FontWeightClass::Bold,
    FontWeightClass::ExtraBold,
    FontWeightClass::ExtraLight,
    FontWeightClass::Light,
    FontWeightClass::Medium,
    FontWeightClass::Regular,
    FontWeightClass::SemiBold,
    FontWeightClass::Thin,
    FontWeightClass::Black,
};

//------------------------------------------------------------------------------------------------------------------------
//                                              FORMATTING & DIFF SUMMARIES
//------------------------------------------------------------------------------------------------------------------------

void FormatHexRgb(ColorQuad Color, char* Buffer, std::size_t Capacity) noexcept
{
    std::snprintf(
        Buffer,
        Capacity,
        "#%02X%02X%02X",
        static_cast<int>(Color.Red * 255.0f + 0.5f),
        static_cast<int>(Color.Green * 255.0f + 0.5f),
        static_cast<int>(Color.Blue * 255.0f + 0.5f));
}

[[nodiscard]] std::string DescribeAppearanceChanges(const AppearanceSettings& Applied, const AppearanceSettings& Draft)
{
    std::string Summary{};
    auto AppendField = [&](bool Changed, const char* Label)
    {
        if (!Changed)
        {
            return;
        }
        if (!Summary.empty())
        {
            Summary += ", ";
        }
        Summary += Label;
    };

    AppendField(Draft.InterfaceScale != Applied.InterfaceScale, "UI scale");
    AppendField(Draft.Theme != Applied.Theme, "Theme");
    AppendField(Draft.CornerRadius != Applied.CornerRadius, "Corner radius");
    AppendField(Draft.Accent != Applied.Accent, "Accent");
    AppendField(Draft.WarningSwatch != Applied.WarningSwatch, "Warning colour");
    AppendField(Draft.SuccessSwatch != Applied.SuccessSwatch, "Success colour");
    AppendField(Draft.NoticeSwatch != Applied.NoticeSwatch, "Notice colour");
    AppendField(Draft.CautionSwatch != Applied.CautionSwatch, "Caution colour");
    AppendField(Draft.TypefaceIndex != Applied.TypefaceIndex, "Typeface");

    bool ScaleChanged   = false;
    bool WeightsChanged = false;
    for (uint32_t Slot = 0u; Slot < AppearanceSettings::TypeSlotCount; ++Slot)
    {
        ScaleChanged   = ScaleChanged || (Draft.SlotSize[Slot] != Applied.SlotSize[Slot]);
        WeightsChanged = WeightsChanged || (Draft.SlotWeight[Slot] != Applied.SlotWeight[Slot]);
    }
    AppendField(ScaleChanged, "Type scale");
    AppendField(WeightsChanged, "Font weights");

    if (Summary.empty())
    {
        return "Apply updated appearance settings to the workspace?";
    }
    return "Apply modified appearance settings (" + Summary + ") to this .projectspace?";
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                          LIFECYCLE & PROJECTSPACE PERSISTENCE
//------------------------------------------------------------------------------------------------------------------------

ControlCentrePanel::ControlCentrePanel() noexcept
    : WidthSpringAxis(SheetSprings.RegisterAxis(920.0, 225.0, 24.0))
    , HeightSpringAxis(SheetSprings.RegisterAxis(480.0, 225.0, 24.0))
{
    SyncAppliedControlColors();
}

void ControlCentrePanel::SyncAppliedControlColors() noexcept
{
    AppliedTheme.ConfigureTheme(AppliedAppearance.Theme);
    AppliedTheme.ConfigureAccent(AppliedAppearance.Accent);
    AppliedTheme.ConfigureCornerRadius(AppliedAppearance.CornerRadius);
    ControlAppearance::ConfigureTheme(
        AppliedTheme,
        ThemeRepository::QuerySemanticColor(0u, AppliedAppearance.WarningSwatch),
        ThemeRepository::QuerySemanticColor(1u, AppliedAppearance.SuccessSwatch),
        ThemeRepository::QuerySemanticColor(2u, AppliedAppearance.NoticeSwatch),
        ThemeRepository::QuerySemanticColor(3u, AppliedAppearance.CautionSwatch));
}

bool ControlCentrePanel::AttachProjectSpace(const std::filesystem::path& RepositoryPath, std::string* OutDiagnostic) noexcept
{
    std::string Diagnostic{};
    if (!ProjectSpaceRepository::ValidateContainerPath(RepositoryPath, Diagnostic))
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = Diagnostic;
        }
        return false;
    }

    AttachedRepositoryPath = RepositoryPath;
    if (!std::filesystem::exists(RepositoryPath))
    {
        if (OutDiagnostic != nullptr)
        {
            OutDiagnostic->clear();
        }
        return SaveToProjectSpace(OutDiagnostic);
    }

    ProjectSpaceDocument Loaded{};
    if (!ProjectSpaceRepository::Load(RepositoryPath, Loaded, Diagnostic))
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = Diagnostic;
        }
        if (AppliedNotifications.ErrorAlertsEnabled)
        {
            Notifications.Broadcast(
                NotificationSeverity::Error,
                "Preferences could not be loaded",
                Diagnostic,
                AppliedNotifications.AlertHoldSeconds,
                true);
        }
        return false;
    }

    AppliedAppearance = Loaded.Appearance;
    if (Typefaces != nullptr)
    {
        const int32_t ResolvedIndex = Typefaces->ResolveGroupIndex(Loaded.TypefaceName);
        if (ResolvedIndex >= 0)
        {
            AppliedAppearance.TypefaceIndex = static_cast<uint32_t>(ResolvedIndex);
        }
    }
    DraftAppearance        = AppliedAppearance;
    AppliedInput           = Loaded.Input;
    DraftInput             = AppliedInput;
    AppliedNotifications   = Loaded.Notifications;
    DraftNotifications     = AppliedNotifications;
    DetachedWindowsAllowed = Loaded.WorkspaceLayout.DetachedWindowsEnabled;
    if (DetachedWindowsCallback)
    {
        DetachedWindowsCallback(DetachedWindowsAllowed);
    }

    SyncAppliedControlColors();
    if (OutDiagnostic != nullptr)
    {
        OutDiagnostic->clear();
    }
    return true;
}

bool ControlCentrePanel::SaveToProjectSpace(std::string* OutDiagnostic) noexcept
{
    if (AttachedRepositoryPath.empty())
    {
        if (OutDiagnostic != nullptr)
        {
            OutDiagnostic->clear();
        }
        return true;
    }

    ProjectSpaceDocument Document{};
    Document.ProjectTitle = AttachedRepositoryPath.stem().string();
    Document.Appearance   = AppliedAppearance;
    Document.TypefaceName = "Inter";
    if (Typefaces != nullptr && Typefaces->QueryGroup(AppliedAppearance.TypefaceIndex) != nullptr)
    {
        Document.TypefaceName = Typefaces->QueryGroup(AppliedAppearance.TypefaceIndex)->Name;
    }
    Document.WorkspaceLayout.DetachedWindowsEnabled = DetachedWindowsAllowed;
    Document.Input                                  = AppliedInput;
    Document.Notifications                          = AppliedNotifications;

    std::string Diagnostic{};
    const bool  Saved = ProjectSpaceRepository::Save(AttachedRepositoryPath, Document, Diagnostic);
    if (OutDiagnostic != nullptr)
    {
        *OutDiagnostic = Diagnostic;
    }
    return Saved;
}

void ControlCentrePanel::ConfigureWorkspaceCallbacks(
    std::function<void(bool)> OnDetachedWindowsChanged,
    std::function<void()>     OnResetLayoutRequested) noexcept
{
    DetachedWindowsCallback = std::move(OnDetachedWindowsChanged);
    ResetLayoutCallback     = std::move(OnResetLayoutRequested);
}

void ControlCentrePanel::ConfigureOpen(bool OpenRequested, bool Immediate) noexcept
{
    if (Immediate)
    {
        Notch.SnapOpen(OpenRequested);
    }
    else
    {
        Notch.ConfigureOpen(OpenRequested);
    }
    if (!OpenRequested)
    {
        InputProfileSelectorOpen = false;
        CaptureShortcutSlot      = -1;
        ActiveSliderOrdinal      = -1;
    }
}

void ControlCentrePanel::ActivatePage(ControlCentrePage Page, bool Immediate) noexcept
{
    ActivePage               = Page;
    ScrollOffset             = 0.0f;
    ActiveSliderOrdinal      = -1;
    InputProfileSelectorOpen = false;
    CaptureShortcutSlot      = -1;
    StatusDiagnostic.clear();

    if (!Notch.QueryOpen())
    {
        ConfigureOpen(true, Immediate);
    }
    if (Immediate)
    {
        SheetGeometrySeeded = false;
    }
}

void ControlCentrePanel::ApplyDraftAppearanceImmediate() noexcept
{
    AppliedAppearance = DraftAppearance;
    SyncAppliedControlColors();

    std::string Diagnostic{};
    const bool  Saved = SaveToProjectSpace(&Diagnostic);
    if (!Saved && AppliedNotifications.ErrorAlertsEnabled)
    {
        Notifications.Broadcast(
            NotificationSeverity::Error,
            "Preferences could not be saved",
            Diagnostic,
            AppliedNotifications.AlertHoldSeconds,
            true);
    }
    else if (Saved && AppliedNotifications.AppliedAlertsEnabled)
    {
        Notifications.Broadcast(
            NotificationSeverity::Success,
            "Preferences saved",
            "Saved appearance and typography to .projectspace.",
            AppliedNotifications.AlertHoldSeconds,
            true);
    }
}

void ControlCentrePanel::ApplyDraftInputImmediate() noexcept
{
    std::string Diagnostic{};
    if (!InputSettingsRepository::ValidatePreferences(DraftInput, Diagnostic))
    {
        StatusDiagnostic = Diagnostic;
        if (AppliedNotifications.ErrorAlertsEnabled)
        {
            Notifications.Broadcast(
                NotificationSeverity::Error,
                "Shortcut conflict",
                Diagnostic,
                AppliedNotifications.AlertHoldSeconds,
                true);
        }
        return;
    }

    AppliedInput = DraftInput;
    StatusDiagnostic.clear();
    const bool Saved = SaveToProjectSpace(&Diagnostic);
    if (Saved && AppliedNotifications.AppliedAlertsEnabled)
    {
        Notifications.Broadcast(
            NotificationSeverity::Success,
            "Preferences saved",
            "Saved input shortcuts to .projectspace.",
            AppliedNotifications.AlertHoldSeconds,
            true);
    }
}

void ControlCentrePanel::ApplyDraftNotificationsImmediate() noexcept
{
    AppliedNotifications = DraftNotifications;
    std::string Diagnostic{};
    const bool  Saved = SaveToProjectSpace(&Diagnostic);
    if (Saved && AppliedNotifications.AppliedAlertsEnabled)
    {
        Notifications.Broadcast(
            NotificationSeverity::Success,
            "Preferences saved",
            "Saved telemetry and alert preferences to .projectspace.",
            AppliedNotifications.AlertHoldSeconds,
            true);
    }
}

bool ControlCentrePanel::OpenConfirmationModal(const ConfirmationSpecification& Specification)
{
    DialogueIntent = PendingDialogueIntent::ExternalCustom;
    return Dialogue.Activate(Specification);
}

void ControlCentrePanel::ConfigureDetachedWindowsAllowed(bool Allowed) noexcept
{
    DetachedWindowsAllowed = Allowed;
    if (DetachedWindowsCallback)
    {
        DetachedWindowsCallback(Allowed);
    }
    (void)SaveToProjectSpace(nullptr);
}

void ControlCentrePanel::BeginInputShortcutCapture(uint32_t SlotIndex) noexcept
{
    if (SlotIndex < InputPreferences::ShortcutCount)
    {
        DraftInput.CustomShortcutsEnabled = true;
        CaptureShortcutSlot               = static_cast<int>(SlotIndex);
        StatusDiagnostic.clear();
    }
}

bool ControlCentrePanel::QueryCurrentPageDirty() const noexcept
{
    switch (ActivePage)
    {
        case ControlCentrePage::Appearance:    return !(DraftAppearance == AppliedAppearance);
        case ControlCentrePage::Input:         return !(DraftInput == AppliedInput);
        case ControlCentrePage::Notifications: return !(DraftNotifications == AppliedNotifications);
        default:                               return false;
    }
}

void ControlCentrePanel::RequestBackNavigation() noexcept
{
    const ControlCentrePage Destination = (ActivePage == ControlCentrePage::Settings)
                                              ? ControlCentrePage::QuickControls
                                              : ControlCentrePage::Settings;
    if (QueryCurrentPageDirty())
    {
        ConfirmationSpecification Specification{};
        Specification.Severity     = DialogueSeverity::Warning;
        Specification.Layout       = DialogueLayoutStyle::Rounded;
        Specification.Title        = "Discard unsaved changes?";
        Specification.Description  = "Leaving this page will revert unapplied edits back to the active configuration.";
        Specification.PrimaryLabel = "Discard";
        if (Dialogue.Activate(Specification))
        {
            DialogueIntent    = PendingDialogueIntent::ConfirmDiscardChanges;
            DiscardTargetPage = Destination;
        }
        return;
    }
    ActivatePage(Destination, false);
}

void ControlCentrePanel::ResolveConfirmationResponse(DialogueResponse Response)
{
    if (Response == DialogueResponse::None)
    {
        return;
    }

    const PendingDialogueIntent Intent = DialogueIntent;
    DialogueIntent                     = PendingDialogueIntent::None;
    if (Response != DialogueResponse::Confirmed)
    {
        return;
    }

    switch (Intent)
    {
        case PendingDialogueIntent::ConfirmApplyAppearance:
            ApplyDraftAppearanceImmediate();
            break;
        case PendingDialogueIntent::ConfirmApplyInput:
            ApplyDraftInputImmediate();
            break;
        case PendingDialogueIntent::ConfirmApplyNotifications:
            ApplyDraftNotificationsImmediate();
            break;
        case PendingDialogueIntent::ConfirmDiscardChanges:
            DraftAppearance    = AppliedAppearance;
            DraftInput         = AppliedInput;
            DraftNotifications = AppliedNotifications;
            SyncAppliedControlColors();
            ActivatePage(DiscardTargetPage, false);
            break;
        case PendingDialogueIntent::None:
        case PendingDialogueIntent::ExternalCustom:
            break;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                               PRIMARY RENDERING & MOTION
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::Render(Rectangle2D HostBoundsPhysical, double Δt, float RefreshRateHertz)
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const float Scale         = QueryInterfaceScaleFactor();
    const float LogicalWidth  = std::max(1.0f, HostBoundsPhysical.Width / Scale);
    const float LogicalHeight = std::max(1.0f, HostBoundsPhysical.Height / Scale);

    if (Notch.QueryOpen() && ActivePage == ControlCentrePage::Appearance)
    {
        ThemeRepository PreviewTheme{};
        PreviewTheme.ConfigureTheme(DraftAppearance.Theme);
        PreviewTheme.ConfigureAccent(DraftAppearance.Accent);
        PreviewTheme.ConfigureCornerRadius(DraftAppearance.CornerRadius);
        ControlAppearance::ConfigureTheme(
            PreviewTheme,
            ThemeRepository::QuerySemanticColor(0u, DraftAppearance.WarningSwatch),
            ThemeRepository::QuerySemanticColor(1u, DraftAppearance.SuccessSwatch),
            ThemeRepository::QuerySemanticColor(2u, DraftAppearance.NoticeSwatch),
            ThemeRepository::QuerySemanticColor(3u, DraftAppearance.CautionSwatch));
    }
    else
    {
        SyncAppliedControlColors();
    }

    const ImGuiIO& Io            = ImGui::GetIO();
    PreviewPointerDelta          = AppliedInput.TransformPointerDelta({ Io.MouseDelta.x, Io.MouseDelta.y });
    const bool     ModalBlocking = Dialogue.QueryOpen();

    if (!ModalBlocking)
    {
        if (CaptureShortcutSlot >= 0)
        {
            ShortcutChord Captured{};
            bool          EscapeCancelled = false;
            if (InputSettingsRepository::PollImGuiChordCapture(Captured, EscapeCancelled))
            {
                InputPreferences Candidate                                          = DraftInput;
                Candidate.CustomShortcutsEnabled                                    = true;
                Candidate.Shortcuts[static_cast<std::size_t>(CaptureShortcutSlot)] = Captured;

                std::string ValidationMessage{};
                if (InputSettingsRepository::ValidatePreferences(Candidate, ValidationMessage))
                {
                    DraftInput          = Candidate;
                    CaptureShortcutSlot = -1;
                    StatusDiagnostic.clear();
                }
                else
                {
                    StatusDiagnostic = ValidationMessage;
                }
            }
            else if (EscapeCancelled)
            {
                CaptureShortcutSlot = -1;
                StatusDiagnostic.clear();
            }
        }
        else
        {
            if (InputSettingsRepository::QueryChordTriggeredInImGui(
                    AppliedInput.QueryActiveShortcut(ShortcutCommand::ControlCentre)))
            {
                ConfigureOpen(!Notch.QueryOpen(), false);
            }
            else if (InputSettingsRepository::QueryChordTriggeredInImGui(
                         AppliedInput.QueryActiveShortcut(ShortcutCommand::Typography)))
            {
                ActivatePage(ControlCentrePage::Appearance, false);
                ActivateAppearanceTab(AppearanceSectionTab::Typography);
            }
            else if (InputSettingsRepository::QueryChordTriggeredInImGui(
                         AppliedInput.QueryActiveShortcut(ShortcutCommand::Theme)))
            {
                ActivatePage(ControlCentrePage::Appearance, false);
                ActivateAppearanceTab(AppearanceSectionTab::Theme);
            }
            else if (Notch.QueryOpen() &&
                     InputSettingsRepository::QueryChordTriggeredInImGui(
                         AppliedInput.QueryActiveShortcut(ShortcutCommand::DismissPanel)))
            {
                if (ActivePage == ControlCentrePage::QuickControls)
                {
                    ConfigureOpen(false, false);
                }
                else
                {
                    RequestBackNavigation();
                }
            }
        }
    }

    Notch.ConfigureExtent(HostBoundsPhysical.Width, HostBoundsPhysical.Height, Scale);
    const float LocalMouseX = Io.MousePos.x - HostBoundsPhysical.X;
    const float LocalMouseY = Io.MousePos.y - HostBoundsPhysical.Y;
    if (!ModalBlocking)
    {
        Notch.IntegratePointer(
            LocalMouseX,
            LocalMouseY,
            ImGui::IsMouseClicked(0),
            ImGui::IsMouseDown(0),
            ImGui::IsMouseReleased(0),
            Δt,
            false);
    }
    Notch.Integrate(Δt);

    {
        const float ScrollRoom = std::max(FontStripContentWidth - FontStripViewWidth, 0.0f);
        FontStripTarget        = std::clamp(FontStripTarget, 0.0f, ScrollRoom);
        const float Gain       = 1.0f - std::exp(-static_cast<float>(std::max(0.0, Δt)) * 18.0f);
        FontStripScroll       += (FontStripTarget - FontStripScroll) * Gain;
        if (std::fabs(FontStripTarget - FontStripScroll) < 0.25f)
        {
            FontStripScroll = FontStripTarget;
        }
    }

    const bool  SubPageActive   = ActivePage != ControlCentrePage::QuickControls && ActivePage != ControlCentrePage::Settings;
    const float MaxSheetWidth   = std::max(280.0f, LogicalWidth - 48.0f);
    const float MaxSheetHeight  = std::max(240.0f, LogicalHeight - 84.0f);
    const float TargetWidth     = SubPageActive
                                      ? MaxSheetWidth
                                      : std::min(ActivePage == ControlCentrePage::QuickControls ? 920.0f : 420.0f, MaxSheetWidth);
    const float TargetHeight    = SubPageActive
                                      ? MaxSheetHeight
                                      : std::min(ActivePage == ControlCentrePage::QuickControls ? 480.0f : 403.0f, MaxSheetHeight);

    if (!SheetGeometrySeeded)
    {
        SheetSprings.Axis(WidthSpringAxis).ConfigurePosition(TargetWidth);
        SheetSprings.Axis(HeightSpringAxis).ConfigurePosition(TargetHeight);
        SheetGeometrySeeded = true;
    }
    else
    {
        SheetSprings.Axis(WidthSpringAxis).Depart(TargetWidth);
        SheetSprings.Axis(HeightSpringAxis).Depart(TargetHeight);
        SheetSprings.Integrate(Δt);
    }

    ImFont* DefaultFont = nullptr;
    if (Typefaces != nullptr)
    {
        DefaultFont = Typefaces->QueryFont(AppliedAppearance.TypefaceIndex, FontWeightClass::Regular);
    }

    PixelSpace Foreground(
        ImGui::GetForegroundDrawList(),
        { HostBoundsPhysical.X, HostBoundsPhysical.Y },
        Scale,
        DefaultFont);

    const ControlColorSet& Colors = ControlAppearance::Colors();

    ControlPointer Pointer{};
    Pointer.X        = LocalMouseX / Scale;
    Pointer.Y        = LocalMouseY / Scale;
    Pointer.Down     = !ModalBlocking && ImGui::IsMouseDown(0);
    Pointer.Pressed  = !ModalBlocking && ImGui::IsMouseClicked(0);
    Pointer.Released = !ModalBlocking && ImGui::IsMouseReleased(0);
    Pointer.Enabled  = !ModalBlocking;

    const float Progress    = Notch.QueryProgress();
    const float ShadeHeight = Notch.QueryHeight() / Scale;

    if (Progress > 0.002f)
    {
        Foreground.FillRectangle(
            { 0.0f, 0.0f, LogicalWidth, LogicalHeight },
            ColorQuad{ 0.0f, 0.0f, 0.0f, 0.40f * Progress }.ToRgba8(),
            0.0f);
        Foreground.FillRectangle(
            { 0.0f, 0.0f, LogicalWidth, ShadeHeight },
            Colors.Field.ToRgba8(),
            0.0f);

        Foreground.PushClip({ 0.0f, 0.0f, LogicalWidth, ShadeHeight });
        const float SheetWidth  = static_cast<float>(SheetSprings.Axis(WidthSpringAxis).Current);
        const float SheetHeight = static_cast<float>(SheetSprings.Axis(HeightSpringAxis).Current);
        const float SheetX      = (LogicalWidth - SheetWidth) * 0.5f;
        const float SheetY      = SubPageActive ? 24.0f : std::max(24.0f, (LogicalHeight - SheetHeight) * 0.5f - 12.0f);

        ControlPointer ClippedPointer = Pointer;
        if (Pointer.Y > ShadeHeight)
        {
            ClippedPointer.Enabled = false;
        }

        ConstructPageContents(Foreground, ActivePage, { SheetX, SheetY, SheetWidth, SheetHeight }, ClippedPointer, Progress);
        Foreground.PopClip();
    }

    Notch.Construct(Foreground, "Frontier", Colors.TabBackground.ToRgba8(), Colors.TextDim.ToRgba8(), { 0.0f, 0.0f });

    if (AppliedNotifications.RateOverlayEnabled)
    {
        char RateLabel[32]{};
        std::snprintf(RateLabel, sizeof(RateLabel), "%.1f FPS", static_cast<double>(RefreshRateHertz));
        const Rectangle2D RateIndicatorRect{ LogicalWidth - 88.0f, 10.0f, 76.0f, 24.0f };
        Foreground.FillRectangle(RateIndicatorRect, Colors.Panel.ToRgba8(), 12.0f);
        ControlAppearance::ConstructRoundedOutline(Foreground, RateIndicatorRect, Colors.StrokeStrong, 12.0f);
        ControlAppearance::TextCentered(Foreground, RateIndicatorRect, Colors.Text, RateLabel, 11.5f);
    }

    Notifications.ConstructToastOverlay(Foreground, { 0.0f, 0.0f, LogicalWidth, LogicalHeight }, Pointer, Δt);

    if (!Dialogue.QueryOpen())
    {
        (void)ControlTooltip::PresentHoveredControlTooltip(
            Foreground,
            { 0.0f, 0.0f, LogicalWidth, LogicalHeight },
            Pointer);
    }
    else
    {
        ControlAppearance::ClearHoveredTooltip();
    }

    if (Dialogue.QueryOpen())
    {
        ControlPointer ModalPointer{};
        ModalPointer.X        = LocalMouseX / Scale;
        ModalPointer.Y        = LocalMouseY / Scale;
        ModalPointer.Down     = ImGui::IsMouseDown(0);
        ModalPointer.Pressed  = ImGui::IsMouseClicked(0);
        ModalPointer.Released = ImGui::IsMouseReleased(0);
        ModalPointer.Enabled  = true;

        const DialogueResponse ModalResult = Dialogue.ConstructModal(
            Foreground,
            { 0.0f, 0.0f, LogicalWidth, LogicalHeight },
            ModalPointer);
        ResolveConfirmationResponse(ModalResult);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                PAGE ROUTING & TOP PAGES
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::ConstructPageContents(
    PixelSpace&           Surface,
    ControlCentrePage     Page,
    Rectangle2D           ContainerBounds,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    if (Page == ControlCentrePage::QuickControls)
    {
        ConstructQuickControlsPage(Surface, ContainerBounds, Pointer, Opacity);
    }
    else if (Page == ControlCentrePage::Settings)
    {
        ConstructSettingsDirectoryPage(Surface, ContainerBounds, Pointer, Opacity);
    }
    else
    {
        ConstructSubPageContainer(Surface, Page, ContainerBounds, Pointer, Opacity);
    }
}

void ControlCentrePanel::ConstructQuickControlsPage(
    PixelSpace&           Surface,
    Rectangle2D           ContainerBounds,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    const ControlColorSet& Colors = ControlAppearance::Colors();

    Surface.Text(
        { ContainerBounds.X, ContainerBounds.Y + 4.0f },
        13.0f,
        Colors.TextDim.Faded(Opacity).ToRgba8(),
        "Quick Controls");

    const Rectangle2D GearHit{ ContainerBounds.Right() - 28.0f, ContainerBounds.Y - 2.0f, 28.0f, 28.0f };
    const bool        GearHover = ControlAppearance::QueryPointerOver(GearHit, Pointer);
    ControlAppearance::ConstructIconCentered(
        Surface,
        GearHit,
        16.0f,
        (GearHover ? Colors.Accent : Colors.Text).Faded(Opacity),
        ControlIcon::SettingsGear,
        2.0f);
    if (GearHover && Pointer.Released)
    {
        ActivatePage(ControlCentrePage::Settings, false);
        return;
    }

    const float TopY            = ContainerBounds.Y + 52.0f;
    const bool  NarrowStack     = ContainerBounds.Width < 560.0f;
    const float LeftColumnWidth = NarrowStack ? ContainerBounds.Width : 244.0f;
    const float DiscDiameter    = 64.0f;
    const float ColumnGap       = 48.0f;
    const float GridLeft        = ContainerBounds.X + (NarrowStack ? (LeftColumnWidth - (DiscDiameter * 2.0f + ColumnGap)) * 0.5f : 18.0f);

    const Rectangle2D RateDisc{ GridLeft, TopY + 12.0f, DiscDiameter, DiscDiameter };
    if (CapsuleButton::ConstructQuickCircleTile(
            Surface,
            RateDisc,
            ControlIcon::GaugeRate,
            "FPS Overlay",
            AppliedNotifications.RateOverlayEnabled,
            Pointer,
            Opacity)
            .Clicked)
    {
        AppliedNotifications.RateOverlayEnabled = !AppliedNotifications.RateOverlayEnabled;
        DraftNotifications.RateOverlayEnabled   = AppliedNotifications.RateOverlayEnabled;
        (void)SaveToProjectSpace(nullptr);
    }

    const bool        AlertsActive = AppliedNotifications.AppliedAlertsEnabled || AppliedNotifications.ErrorAlertsEnabled;
    const Rectangle2D BellDisc{ GridLeft + DiscDiameter + ColumnGap, TopY + 12.0f, DiscDiameter, DiscDiameter };
    if (CapsuleButton::ConstructQuickCircleTile(
            Surface,
            BellDisc,
            ControlIcon::NotificationsBell,
            "Notifications",
            AlertsActive,
            Pointer,
            Opacity)
            .Clicked)
    {
        const bool NextEnabled                    = !AlertsActive;
        AppliedNotifications.AppliedAlertsEnabled = NextEnabled;
        AppliedNotifications.ErrorAlertsEnabled   = NextEnabled;
        DraftNotifications                        = AppliedNotifications;
        (void)SaveToProjectSpace(nullptr);
    }

    Rectangle2D StreamRegion{};
    if (NarrowStack)
    {
        StreamRegion = {
            ContainerBounds.X,
            TopY + 132.0f,
            ContainerBounds.Width,
            std::max(120.0f, ContainerBounds.Bottom() - (TopY + 132.0f)),
        };
    }
    else
    {
        const float DividerX = ContainerBounds.X + LeftColumnWidth + 12.0f;
        Surface.FillRectangle(
            { DividerX, TopY, 1.0f, ContainerBounds.Height - 52.0f },
            Colors.Divider.Faded(Opacity).ToRgba8(),
            0.0f);
        StreamRegion = {
            DividerX + 20.0f,
            TopY,
            ContainerBounds.Right() - (DividerX + 20.0f),
            ContainerBounds.Height - 52.0f,
        };
    }

    const auto& History = Notifications.QueryHistory();
    char        StreamTitle[64]{};
    std::snprintf(StreamTitle, sizeof(StreamTitle), "Notification Stream (%zu)", History.size());
    Surface.Text({ StreamRegion.X, StreamRegion.Y + 6.0f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), StreamTitle);

    const float       ClearWidth = CapsuleButton::MeasureChipWidth(Surface, "Clear history");
    const Rectangle2D ClearRect{ StreamRegion.Right() - ClearWidth, StreamRegion.Y, ClearWidth, ControlMetrics::ChipHeight };
    if (CapsuleButton::ConstructChip(Surface, ClearRect, "Clear history", false, Pointer, Opacity).Clicked)
    {
        Notifications.ClearHistory();
    }

    const Rectangle2D StreamViewport{
        StreamRegion.X,
        StreamRegion.Y + 40.0f,
        StreamRegion.Width,
        std::max(40.0f, StreamRegion.Height - 40.0f),
    };
    Surface.PushClip(StreamViewport);

    if (History.empty())
    {
        ControlAppearance::TextCentered(Surface, StreamViewport, Colors.TextFaint.Faded(Opacity), "No recent notifications", 13.0f);
    }
    else
    {
        float TotalHeight = 0.0f;
        for (auto Iterator = History.rbegin(); Iterator != History.rend(); ++Iterator)
        {
            TotalHeight += NotificationSequence::QueryBannerHeight(Surface, StreamViewport.Width, *Iterator) + 12.0f;
        }
        if (ControlAppearance::QueryPointerOver(StreamViewport, Pointer))
        {
            StreamScrollOffset = ControlAppearance::AdvanceScrollOffset(
                StreamScrollOffset,
                Surface.QueryMouseWheelY(),
                TotalHeight,
                StreamViewport.Height);
        }

        float CursorY = StreamViewport.Y - StreamScrollOffset;
        for (auto Iterator = History.rbegin(); Iterator != History.rend(); ++Iterator)
        {
            const float BannerHeight = NotificationSequence::QueryBannerHeight(Surface, StreamViewport.Width, *Iterator);
            (void)NotificationSequence::ConstructBanner(
                Surface,
                { StreamViewport.X, CursorY, StreamViewport.Width, BannerHeight },
                *Iterator,
                Pointer,
                false);
            CursorY += BannerHeight + 12.0f;
        }
        ControlAppearance::ConstructScrollIndicator(Surface, StreamViewport, StreamScrollOffset, TotalHeight, Opacity);
    }
    Surface.PopClip();
}

void ControlCentrePanel::ConstructSettingsDirectoryPage(
    PixelSpace&           Surface,
    Rectangle2D           ContainerBounds,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    const ControlColorSet& Colors = ControlAppearance::Colors();

    const Rectangle2D BackHit{ ContainerBounds.X, ContainerBounds.Y, 32.0f, 32.0f };
    const bool        BackHover = ControlAppearance::QueryPointerOver(BackHit, Pointer);
    ControlAppearance::ConstructIconCentered(
        Surface,
        BackHit,
        22.0f,
        (BackHover ? Colors.Accent : Colors.Text).Faded(Opacity),
        ControlIcon::ChevronBack,
        2.0f);
    if (BackHover && Pointer.Released)
    {
        ActivatePage(ControlCentrePage::QuickControls, false);
        return;
    }
    Surface.Text({ ContainerBounds.X + 34.0f, ContainerBounds.Y + 3.0f }, 22.0f, Colors.Text.Faded(Opacity).ToRgba8(), "Settings");

    struct DirectoryRowEntry
    {
        ControlIcon       Icon;     // [-] - Leading vector icon
        const char*       Title;    // [-] - Primary row title
        const char*       Subtitle; // [-] - Secondary row description
        ControlCentrePage Target;   // [-] - Destination page
    };

    constexpr DirectoryRowEntry Rows[4] = {
        { ControlIcon::AppearanceSwatches, "Appearance",    "Typography, theme, and interface scale", ControlCentrePage::Appearance    },
        { ControlIcon::ShieldInput,        "Input",         "Shortcuts and pointer response",         ControlCentrePage::Input         },
        { ControlIcon::NotificationsBell,  "Notifications", "FPS overlay and interface alerts",       ControlCentrePage::Notifications },
        { ControlIcon::DisplayMonitor,     "Workspace",     "Docked tabs and optional OS windows",    ControlCentrePage::Workspace     },
    };

    const float       RowHeight = 76.0f;
    const Rectangle2D DirectoryContainer{ ContainerBounds.X, ContainerBounds.Y + 56.0f, ContainerBounds.Width, RowHeight * 4.0f };
    Surface.FillRectangle(DirectoryContainer, Colors.Raised.Faded(Opacity).ToRgba8(), 24.0f);
    ControlAppearance::ConstructRoundedOutline(Surface, DirectoryContainer, Colors.Stroke.Faded(Opacity), 24.0f);

    Surface.PushClip(DirectoryContainer);
    for (std::size_t Index = 0u; Index < 4u; ++Index)
    {
        const Rectangle2D RowRect{
            DirectoryContainer.X,
            DirectoryContainer.Y + static_cast<float>(Index) * RowHeight,
            DirectoryContainer.Width,
            RowHeight,
        };
        const bool Hover = ControlAppearance::QueryPointerOver(RowRect, Pointer);
        if (Hover)
        {
            const ColorQuad HoverFill = Colors.LightSurface
                                            ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.04f }
                                            : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.04f };
            Surface.FillRectangle(RowRect, HoverFill.Faded(Opacity).ToRgba8(), 0.0f);
        }

        const Rectangle2D IconDisc{ RowRect.X + 20.0f, RowRect.Y + (RowHeight - 44.0f) * 0.5f, 44.0f, 44.0f };
        Surface.FillRectangle(IconDisc, Colors.Selected.Faded(Opacity).ToRgba8(), 22.0f);
        ControlAppearance::ConstructIconCentered(Surface, IconDisc, 20.0f, Colors.Text.Faded(Opacity), Rows[Index].Icon, 2.0f);

        Surface.Text({ RowRect.X + 80.0f, RowRect.Y + 19.0f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), Rows[Index].Title);
        Surface.Text({ RowRect.X + 80.0f, RowRect.Y + 41.0f }, 12.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), Rows[Index].Subtitle);
        ControlAppearance::ConstructIcon(
            Surface,
            RowRect.Right() - 36.0f,
            RowRect.Y + (RowHeight - 18.0f) * 0.5f,
            18.0f,
            Colors.TextDim.Faded(Opacity),
            ControlIcon::ChevronForward,
            2.0f);

        if (Index < 3u)
        {
            ControlAppearance::ConstructHairline(Surface, RowRect.X + 20.0f, RowRect.Bottom(), RowRect.Width - 40.0f, Colors.Divider.Faded(Opacity));
        }
        if (Hover && Pointer.Released)
        {
            ActivatePage(Rows[Index].Target, false);
            break;
        }
    }
    Surface.PopClip();
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SUB-PAGE CONTAINER & FOOTER
//------------------------------------------------------------------------------------------------------------------------

void ControlCentrePanel::ConstructSubPageContainer(
    PixelSpace&           Surface,
    ControlCentrePage     Page,
    Rectangle2D           ContainerBounds,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    const ControlColorSet& Colors      = ControlAppearance::Colors();
    constexpr float        SheetRadius = 32.0f;

    Surface.FillRectangle(ContainerBounds, Colors.Panel.Faded(Opacity).ToRgba8(), SheetRadius);
    ControlAppearance::ConstructRoundedOutline(Surface, ContainerBounds, Colors.Stroke.Faded(Opacity), SheetRadius);

    const char* TitleText    = "Appearance";
    const char* SubtitleText = "Customize your workspace visual experience";
    if (Page == ControlCentrePage::Input)
    {
        TitleText    = "Input Settings";
        SubtitleText = "Configure keyboard shortcuts and pointer response";
    }
    else if (Page == ControlCentrePage::Notifications)
    {
        TitleText    = "Telemetry & Notifications";
        SubtitleText = "Overlay and alerts — persisted inside .projectspace";
    }
    else if (Page == ControlCentrePage::Workspace)
    {
        TitleText    = "Workspace";
        SubtitleText = "Panel hosting — persisted inside .projectspace";
    }

    const float HorizontalPadding = 40.0f;
    Surface.Text({ ContainerBounds.X + HorizontalPadding, ContainerBounds.Y + 34.0f }, 24.0f, Colors.Text.Faded(Opacity).ToRgba8(), TitleText);
    Surface.Text({ ContainerBounds.X + HorizontalPadding, ContainerBounds.Y + 66.0f }, 14.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), SubtitleText);

    if (CapsuleButton::ConstructRoundIcon(
            Surface,
            ContainerBounds.Right() - 48.0f,
            ContainerBounds.Y + 48.0f,
            ControlIcon::ChevronBack,
            Pointer,
            Opacity)
            .Clicked)
    {
        RequestBackNavigation();
        return;
    }

    float HeaderBottomY = ContainerBounds.Y + 104.0f;
    if (Page == ControlCentrePage::Appearance)
    {
        constexpr const char* TabNames[3] = { "Display", "Typography", "Theme" };
        float                 TabCursorX  = ContainerBounds.X + HorizontalPadding;
        const float           TabRowY     = ContainerBounds.Y + 104.0f;
        for (uint32_t TabIndex = 0u; TabIndex < 3u; ++TabIndex)
        {
            const float       LabelWidth = Surface.MeasureTextWidth(14.0f, TabNames[TabIndex]);
            const Rectangle2D TabHit{ TabCursorX, TabRowY, LabelWidth + 8.0f, 36.0f };
            const bool        Selected = static_cast<uint32_t>(ActiveAppearanceTab) == TabIndex;
            const bool        Hover    = ControlAppearance::QueryPointerOver(TabHit, Pointer);
            Surface.Text(
                { TabCursorX, TabRowY + 6.0f },
                14.0f,
                (Selected ? Colors.Text : (Hover ? Colors.Text : Colors.TextDim)).Faded(Opacity).ToRgba8(),
                TabNames[TabIndex]);
            if (Selected)
            {
                Surface.FillRectangle({ TabCursorX, TabRowY + 34.0f, LabelWidth, 2.0f }, Colors.Primary.Faded(Opacity).ToRgba8(), 1.0f);
            }
            if (Hover && Pointer.Released)
            {
                ActivateAppearanceTab(static_cast<AppearanceSectionTab>(TabIndex));
            }
            TabCursorX += LabelWidth + 32.0f;
        }
        HeaderBottomY = TabRowY + 36.0f;
    }

    ControlAppearance::ConstructHairline(Surface, ContainerBounds.X, HeaderBottomY, ContainerBounds.Width, Colors.Divider.Faded(Opacity));

    const bool        FooterVisible = (Page != ControlCentrePage::Workspace);
    const float       FooterHeight  = FooterVisible ? 88.0f : 0.0f;
    const Rectangle2D BodyViewport{
        ContainerBounds.X + HorizontalPadding,
        HeaderBottomY + 28.0f,
        std::max(80.0f, ContainerBounds.Width - HorizontalPadding * 2.0f),
        std::max(80.0f, ContainerBounds.Bottom() - FooterHeight - (HeaderBottomY + 28.0f)),
    };

    if (ControlAppearance::QueryPointerOver(BodyViewport, Pointer) && !InputProfileSelectorOpen)
    {
        ScrollOffset = ControlAppearance::AdvanceScrollOffset(
            ScrollOffset,
            Surface.QueryMouseWheelY(),
            LastContentHeight,
            BodyViewport.Height);
    }

    ControlPointer BodyPointer = Pointer;
    if (!BodyViewport.Contains({ Pointer.X, Pointer.Y }))
    {
        BodyPointer.Enabled = false;
    }

    Surface.PushClip({
        ContainerBounds.X + 1.0f,
        HeaderBottomY + 1.0f,
        ContainerBounds.Width - 2.0f,
        ContainerBounds.Bottom() - FooterHeight - (HeaderBottomY + 1.0f),
    });

    if (Page == ControlCentrePage::Appearance)
    {
        if (ActiveAppearanceTab == AppearanceSectionTab::Display)
        {
            LastContentHeight = ConstructAppearanceDisplayBody(Surface, BodyViewport, ScrollOffset, BodyPointer, Opacity);
        }
        else if (ActiveAppearanceTab == AppearanceSectionTab::Typography)
        {
            LastContentHeight = ConstructAppearanceTypographyBody(Surface, BodyViewport, ScrollOffset, BodyPointer, Opacity);
        }
        else
        {
            LastContentHeight = ConstructAppearanceThemeBody(Surface, BodyViewport, ScrollOffset, BodyPointer, Opacity);
        }
    }
    else if (Page == ControlCentrePage::Input)
    {
        LastContentHeight = ConstructInputSettingsBody(Surface, BodyViewport, ScrollOffset, BodyPointer, Opacity);
    }
    else if (Page == ControlCentrePage::Notifications)
    {
        LastContentHeight = ConstructNotificationsBody(Surface, BodyViewport, ScrollOffset, BodyPointer, Opacity);
    }
    else if (Page == ControlCentrePage::Workspace)
    {
        LastContentHeight = ConstructWorkspaceBody(Surface, BodyViewport, ScrollOffset, BodyPointer, Opacity);
    }

    ControlAppearance::ConstructScrollIndicator(
        Surface,
        { ContainerBounds.X + 4.0f, BodyViewport.Y, ContainerBounds.Width - 8.0f, BodyViewport.Height },
        ScrollOffset,
        LastContentHeight,
        Opacity);
    Surface.PopClip();

    if (Page == ControlCentrePage::Input)
    {
        ConstructInputProfileOverlay(Surface, Pointer, Opacity);
    }

    if (FooterVisible)
    {
        const Rectangle2D FooterRect{ ContainerBounds.X, ContainerBounds.Bottom() - FooterHeight, ContainerBounds.Width, FooterHeight };
        Surface.FillRectangleBottomRounded(FooterRect, Colors.SectionSurface.Faded(Opacity).ToRgba8(), SheetRadius);
        ControlAppearance::ConstructHairline(Surface, FooterRect.X, FooterRect.Y, FooterRect.Width, Colors.Divider.Faded(Opacity));

        const bool      Dirty      = QueryCurrentPageDirty();
        const char*     StatusLine = !StatusDiagnostic.empty()
                                         ? StatusDiagnostic.c_str()
                                         : (Dirty ? "Unsaved changes — Apply persists to .projectspace"
                                                  : "All settings synchronized with .projectspace");
        const ColorQuad StatusInk  = !StatusDiagnostic.empty() ? Colors.Danger : Colors.TextDim;
        ControlAppearance::TextLeading(
            Surface,
            { FooterRect.X + HorizontalPadding, FooterRect.Y, FooterRect.Width * 0.55f, FooterHeight },
            0.0f,
            StatusInk.Faded(Opacity),
            StatusLine,
            11.5f);

        const float ButtonY     = FooterRect.Y + (FooterHeight - 40.0f) * 0.5f;
        const char* PrimaryText = (Page == ControlCentrePage::Appearance) ? "Apply" : "Save";

        const ButtonDescriptor ApplyButton{ PrimaryText, ButtonTone::Primary, 13.5f, 24.0f, !Dirty, {} };
        const float            ApplyWidth = CapsuleButton::MeasureWidth(Surface, ApplyButton);
        const Rectangle2D      ApplyRect{ FooterRect.Right() - HorizontalPadding - ApplyWidth, ButtonY, ApplyWidth, 40.0f };
        if (CapsuleButton::Construct(Surface, ApplyRect, ApplyButton, Pointer, Opacity).Clicked)
        {
            ConfirmationSpecification Specification{};
            Specification.Severity     = DialogueSeverity::Notice;
            Specification.Layout       = DialogueLayoutStyle::Rounded;
            Specification.Title        = "Apply changes?";
            Specification.PrimaryLabel = PrimaryText;
            if (Page == ControlCentrePage::Appearance)
            {
                Specification.Description = DescribeAppearanceChanges(AppliedAppearance, DraftAppearance);
                if (Dialogue.Activate(Specification))
                {
                    DialogueIntent = PendingDialogueIntent::ConfirmApplyAppearance;
                }
            }
            else if (Page == ControlCentrePage::Input)
            {
                std::string ValidationMessage{};
                if (!InputSettingsRepository::ValidatePreferences(DraftInput, ValidationMessage))
                {
                    StatusDiagnostic = ValidationMessage;
                }
                else
                {
                    Specification.Description = "Save updated keyboard shortcuts and pointer response to .projectspace?";
                    if (Dialogue.Activate(Specification))
                    {
                        DialogueIntent = PendingDialogueIntent::ConfirmApplyInput;
                    }
                }
            }
            else if (Page == ControlCentrePage::Notifications)
            {
                Specification.Description = "Save telemetry overlay and alert preferences to .projectspace?";
                if (Dialogue.Activate(Specification))
                {
                    DialogueIntent = PendingDialogueIntent::ConfirmApplyNotifications;
                }
            }
        }

        const ButtonDescriptor DiscardButton{ "Discard", ButtonTone::Secondary, 13.5f, 22.0f, !Dirty, {} };
        const float            DiscardWidth = CapsuleButton::MeasureWidth(Surface, DiscardButton);
        const Rectangle2D      DiscardRect{ ApplyRect.X - 12.0f - DiscardWidth, ButtonY, DiscardWidth, 40.0f };
        if (CapsuleButton::Construct(Surface, DiscardRect, DiscardButton, Pointer, Opacity).Clicked)
        {
            if (Page == ControlCentrePage::Appearance)
            {
                DraftAppearance = AppliedAppearance;
                SyncAppliedControlColors();
            }
            else if (Page == ControlCentrePage::Input)
            {
                DraftInput          = AppliedInput;
                CaptureShortcutSlot = -1;
                StatusDiagnostic.clear();
            }
            else if (Page == ControlCentrePage::Notifications)
            {
                DraftNotifications = AppliedNotifications;
            }
        }

        if (Page == ControlCentrePage::Input)
        {
            const ButtonDescriptor DefaultsButton{ "Defaults", ButtonTone::Ghost, 13.5f, 18.0f, false, {} };
            const float            DefaultsWidth = CapsuleButton::MeasureWidth(Surface, DefaultsButton);
            const Rectangle2D      DefaultsRect{ DiscardRect.X - 10.0f - DefaultsWidth, ButtonY, DefaultsWidth, 40.0f };
            if (CapsuleButton::Construct(Surface, DefaultsRect, DefaultsButton, Pointer, Opacity).Clicked)
            {
                DraftInput          = InputPreferences{};
                CaptureShortcutSlot = -1;
                StatusDiagnostic.clear();
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                               APPEARANCE INSPECTOR TABS
//------------------------------------------------------------------------------------------------------------------------

float ControlCentrePanel::ConstructAppearanceDisplayBody(
    PixelSpace&           Surface,
    Rectangle2D           BodyBounds,
    float                 ScrollY,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    const ControlColorSet& Colors  = ControlAppearance::Colors();
    const float            Radius  = std::clamp(DraftAppearance.CornerRadius, 0.0f, 32.0f);
    const Rectangle2D      Content = ControlAppearance::ConstructSectionContainer(
        Surface,
        { BodyBounds.X, BodyBounds.Y - ScrollY, BodyBounds.Width, 180.0f },
        Radius,
        Opacity);

    ControlAppearance::ConstructSectionHeading(
        Surface,
        Content.X,
        Content.Y,
        Content.Width,
        "Interface Scaling",
        "Editor interface only - no scene resolution controls",
        Colors.Text,
        Colors.TextDim,
        Opacity);

    const float       RowY    = Content.Y + 80.0f;
    const Rectangle2D Control = ControlAppearance::ConstructLabeledRow(Surface, Content.X, RowY, Content.Width, "UI Scale", Colors.TextDim, Opacity);

    char NumberBuffer[16]{};
    std::snprintf(NumberBuffer, sizeof(NumberBuffer), "%d", static_cast<int>(std::lround(DraftAppearance.InterfaceScale)));

    float                UpdatedScale = DraftAppearance.InterfaceScale;
    const ControlContact Hit          = ScalarSlider::ConstructWithReadout(
        Surface,
        Control,
        NumberBuffer,
        "%",
        75.0f,
        150.0f,
        UpdatedScale,
        ActiveSliderOrdinal == 0,
        Pointer,
        UpdatedScale,
        Opacity);

    if (Hit.Pressed)
    {
        ActiveSliderOrdinal = 0;
    }
    if (ActiveSliderOrdinal == 0 && Pointer.Down)
    {
        DraftAppearance.InterfaceScale = std::round(UpdatedScale);
    }
    if (Pointer.Released || !Pointer.Down)
    {
        ActiveSliderOrdinal = -1;
    }

    return 180.0f + 24.0f;
}

float ControlCentrePanel::ConstructAppearanceTypographyBody(
    PixelSpace&           Surface,
    Rectangle2D           BodyBounds,
    float                 ScrollY,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    const ControlColorSet& Colors     = ControlAppearance::Colors();
    const float            Radius     = std::clamp(DraftAppearance.CornerRadius, 0.0f, 32.0f);
    const float            LeftX      = BodyBounds.X;
    const float            BodyWidth  = BodyBounds.Width;
    float                  CursorY    = BodyBounds.Y - ScrollY;
    const ColorQuad        Accent     = ThemeRepository::QueryAccentColor(DraftAppearance.Accent);
    const uint32_t         GroupCount = (Typefaces != nullptr) ? Typefaces->QueryGroupCount() : 0u;

    if (Typefaces != nullptr && DraftAppearance.TypefaceIndex >= GroupCount && GroupCount > 0u)
    {
        DraftAppearance.TypefaceIndex = 0u;
    }

    auto ResolveDraftFace = [&](FontWeightClass Weight) noexcept -> ImFont*
    {
        return (Typefaces != nullptr) ? Typefaces->QueryFont(DraftAppearance.TypefaceIndex, Weight) : nullptr;
    };

    const char* SelectedName = (Typefaces != nullptr && Typefaces->QueryGroup(DraftAppearance.TypefaceIndex) != nullptr)
                                   ? Typefaces->QueryGroup(DraftAppearance.TypefaceIndex)->Name.c_str()
                                   : "Inter";
    const ColorQuad SubtleRing = Colors.LightSurface
                                     ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.10f }
                                     : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.10f };
    const ColorQuad SubtleWash = Colors.LightSurface
                                     ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.04f }
                                     : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.05f };
    constexpr float SectionSpacing = 40.0f;

    // ① Typeface selector strip
    {
        const float HeaderHeight  = 40.0f;
        Surface.Text({ LeftX, CursorY + 2.0f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), "Typography");
        Surface.Text({ LeftX, CursorY + 24.0f }, 12.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "Typeface, type scale, and font weights");

        const float ButtonCenterY = CursorY + HeaderHeight - 16.0f;
        if (CapsuleButton::ConstructRoundIcon(Surface, LeftX + BodyWidth - 32.0f - 8.0f - 16.0f, ButtonCenterY, ControlIcon::ChevronBack, Pointer, Opacity).Clicked)
        {
            FontStripTarget -= 300.0f;
        }
        if (CapsuleButton::ConstructRoundIcon(Surface, LeftX + BodyWidth - 16.0f, ButtonCenterY, ControlIcon::ChevronForward, Pointer, Opacity).Clicked)
        {
            FontStripTarget += 300.0f;
        }
        CursorY += HeaderHeight + 16.0f;

        const float TileWidth  = 192.0f;
        const float TileGap    = 16.0f;
        const float TilePad    = 20.0f;
        const float TileHeight = TilePad + 32.0f + 16.0f + 20.0f + 4.0f + 16.0f + TilePad;

        FontStripViewWidth    = BodyWidth;
        FontStripContentWidth = GroupCount > 0u ? static_cast<float>(GroupCount) * (TileWidth + TileGap) - TileGap : 0.0f;

        const Rectangle2D StripBounds{ LeftX, CursorY, BodyWidth, TileHeight };
        Surface.PushClip({ StripBounds.X, StripBounds.Y - 2.0f, StripBounds.Width, StripBounds.Height + 4.0f });
        for (uint32_t Index = 0u; Index < GroupCount; ++Index)
        {
            const TypefaceGroup* Group = Typefaces->QueryGroup(Index);
            const Rectangle2D    TileRect{
                LeftX + static_cast<float>(Index) * (TileWidth + TileGap) - FontStripScroll,
                CursorY,
                TileWidth,
                TileHeight,
            };
            const bool     Active       = (DraftAppearance.TypefaceIndex == Index);
            ControlPointer StripPointer = Pointer;
            StripPointer.Enabled        = Pointer.Enabled && StripBounds.Contains({ Pointer.X, Pointer.Y });
            const bool     Hover        = ControlAppearance::QueryPointerOver(TileRect, StripPointer);

            if (Active || Hover)
            {
                Surface.FillRectangle(TileRect, SubtleWash.Faded(Opacity).ToRgba8(), Radius);
            }
            ControlAppearance::ConstructRoundedOutline(Surface, TileRect, (Active ? Accent : SubtleRing).Faded(Opacity), Radius);

            Surface.PushTypeface(Typefaces->QueryFont(Index, FontWeightClass::Regular));
            Surface.Text({ TileRect.X + TilePad, TileRect.Y + TilePad }, 24.0f, Colors.Text.Faded(Opacity).ToRgba8(), "Aa");
            Surface.PopTypeface();

            Surface.PushTypeface(ResolveDraftFace(FontWeightClass::Medium));
            Surface.Text({ TileRect.X + TilePad, TileRect.Y + TilePad + 50.0f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), Group->Name.c_str());
            Surface.PopTypeface();

            Surface.PushClip({ TileRect.X + TilePad, TileRect.Y, TileRect.Width - TilePad * 2.0f, TileRect.Height });
            Surface.Text({ TileRect.X + TilePad, TileRect.Y + TilePad + 74.0f }, 12.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "The quick brown fox jumps");
            Surface.PopClip();

            if (Hover && StripPointer.Released)
            {
                DraftAppearance.TypefaceIndex = Index;
            }
        }
        Surface.PopClip();
        CursorY += TileHeight + 16.0f + SectionSpacing;
    }

    // ② Typeface & Colors playground
    {
        const float HeaderHeight  = 44.0f;
        const float ColumnWidth   = 280.0f;
        const float ColumnHeight  = (3.0f * 18.0f + 2.0f * 4.0f) + 32.0f + (16.0f + 4.0f + 16.0f + 16.0f) + 32.0f + (48.0f + 8.0f + 4.0f + 2.0f * 13.0f + 2.0f);
        const float PreviewHeight = 188.0f;
        const float InnerHeight   = std::max(ColumnHeight, PreviewHeight);
        const float BoxHeight     = ControlMetrics::SectionPadding * 2.0f + HeaderHeight + InnerHeight;

        const Rectangle2D Content = ControlAppearance::ConstructSectionContainer(Surface, { LeftX, CursorY, BodyWidth, BoxHeight }, Radius, Opacity);
        Surface.Text({ Content.X, Content.Y + 2.0f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), "Typeface & Colors");

        const float       TopY = Content.Y + HeaderHeight;
        const Rectangle2D SpecimenBox{ Content.X, TopY, std::max(80.0f, Content.Width - 32.0f - ColumnWidth), InnerHeight };
        Surface.FillRectangle(SpecimenBox, SubtleWash.Faded(Opacity).ToRgba8(), Radius * 0.8f);
        ControlAppearance::ConstructRoundedOutline(Surface, SpecimenBox, SubtleRing.Faded(Opacity), Radius * 0.8f);

        Surface.PushClip(SpecimenBox);
        const float SpecimenTopY = SpecimenBox.Center().Y - (72.0f + 16.0f + 20.0f) * 0.5f;
        Surface.PushTypeface(ResolveDraftFace(FontWeightClass::Bold));
        Surface.Text({ SpecimenBox.X + 40.0f, SpecimenTopY }, 72.0f, Colors.Text.Faded(Opacity).ToRgba8(), SelectedName);
        Surface.PopTypeface();
        Surface.Text({ SpecimenBox.X + 40.0f, SpecimenTopY + 88.0f }, 14.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "(72px bold)");
        Surface.PopClip();

        const float ColumnX = Content.Right() - ColumnWidth;
        float       ColumnY = TopY + (InnerHeight - ColumnHeight) * 0.5f;

        ImFont* MonoFace = nullptr;
        if (Typefaces != nullptr)
        {
            const int32_t MonoIndex = Typefaces->ResolveGroupIndex("JetBrains Mono");
            if (MonoIndex >= 0)
            {
                MonoFace = Typefaces->QueryFont(static_cast<uint32_t>(MonoIndex), FontWeightClass::Regular);
            }
        }
        Surface.PushTypeface(MonoFace);
        Surface.Text({ ColumnX, ColumnY }, 11.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        Surface.Text({ ColumnX, ColumnY + 22.0f }, 11.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "abcdefghijklmnopqrstuvwxyz");
        Surface.Text({ ColumnX, ColumnY + 44.0f }, 11.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "0123456789 !@#$%^&*()");
        Surface.PopTypeface();
        ColumnY += 94.0f;

        char HexText[12]{};
        FormatHexRgb(Accent, HexText, sizeof(HexText));
        Surface.PushTypeface(ResolveDraftFace(FontWeightClass::SemiBold));
        Surface.Text({ ColumnX, ColumnY }, 12.0f, Accent.Faded(Opacity).ToRgba8(), "Accent");
        const float AccentWordWidth = Surface.MeasureTextWidth(12.0f, "Accent");
        Surface.PopTypeface();

        Surface.PushTypeface(ResolveDraftFace(FontWeightClass::Regular));
        Surface.Text({ ColumnX + AccentWordWidth, ColumnY }, 12.0f, Accent.Faded(Opacity).ToRgba8(), " \xE2\x80\x94 The quick brown fox jumps over...");
        char RenderedInLine[64]{};
        std::snprintf(RenderedInLine, sizeof(RenderedInLine), "(rendered in %s)", HexText);
        ColorQuad DimAccent = Accent;
        DimAccent.Alpha    *= 0.70f;
        Surface.Text({ ColumnX, ColumnY + 20.0f }, 12.0f, DimAccent.Faded(Opacity).ToRgba8(), RenderedInLine);
        Surface.PopTypeface();
        ColumnY += 84.0f;

        struct ColorCircleSpecimen
        {
            ColorQuad   Tone;    // [-] - Swatch fill colour
            const char* Caption; // [-] - Swatch label
        };
        const ColorCircleSpecimen Circles[3] = {
            { Accent,                              "Accent"     },
            { ColorQuad{ 1.0f, 1.0f, 1.0f, 1.0f }, "Primary"    },
            { ColorQuad{ 0.0f, 0.0f, 0.0f, 1.0f }, "Background" },
        };
        float DiscX = ColumnX;
        for (const ColorCircleSpecimen& Circle : Circles)
        {
            ControlAppearance::ConstructCircleFill(Surface, DiscX + 24.0f, ColumnY + 24.0f, 24.0f, Circle.Tone.Faded(Opacity));
            ControlAppearance::ConstructCircleOutline(Surface, DiscX + 24.0f, ColumnY + 24.0f, 23.5f, SubtleRing.Faded(Opacity), 1.0f);
            FormatHexRgb(Circle.Tone, HexText, sizeof(HexText));
            Surface.PushTypeface(ResolveDraftFace(FontWeightClass::SemiBold));
            Surface.Text({ DiscX, ColumnY + 60.0f }, 10.0f, Colors.Text.Faded(Opacity).ToRgba8(), HexText);
            Surface.PopTypeface();
            Surface.Text({ DiscX, ColumnY + 75.0f }, 10.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), Circle.Caption);
            DiscX += std::max(48.0f, Surface.MeasureTextWidth(10.0f, Circle.Caption)) + 24.0f;
        }
        CursorY += BoxHeight + SectionSpacing;
    }

    // ③ TYPE SCALE - DESKTOP (6 slots)
    {
        Surface.Text({ LeftX, CursorY + 2.0f }, 12.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "TYPE SCALE - DESKTOP");
        CursorY += 56.0f;

        for (uint32_t Slot = 0u; Slot < AppearanceSettings::TypeSlotCount; ++Slot)
        {
            uint32_t        ChipCount = 0u;
            FontWeightClass AvailableChips[9]{};
            float           ChipWidths[9]{};
            for (FontWeightClass CandidateWeight : TypographyChipOrder)
            {
                if (Typefaces == nullptr || Typefaces->ContainsWeight(DraftAppearance.TypefaceIndex, CandidateWeight))
                {
                    AvailableChips[ChipCount] = CandidateWeight;
                    ChipWidths[ChipCount]     = CapsuleButton::MeasureChipWidth(Surface, TypographyRepository::WeightLabel(CandidateWeight));
                    ++ChipCount;
                }
            }
            if (Typefaces != nullptr)
            {
                if (const TypefaceWeightEntry* Snapped = Typefaces->ResolveWeight(DraftAppearance.TypefaceIndex, DraftAppearance.SlotWeight[Slot]))
                {
                    DraftAppearance.SlotWeight[Slot] = Snapped->Weight;
                }
            }

            const float LeftColumnWidth = 300.0f;
            uint32_t    RowsNeeded      = 1u;
            {
                float RowRunX = 0.0f;
                for (uint32_t ChipIndex = 0u; ChipIndex < ChipCount; ++ChipIndex)
                {
                    if (RowRunX > 0.0f && RowRunX + ChipWidths[ChipIndex] > LeftColumnWidth)
                    {
                        ++RowsNeeded;
                        RowRunX = 0.0f;
                    }
                    RowRunX += ChipWidths[ChipIndex] + 12.0f;
                }
            }

            const float LeftHeight  = 20.0f + 24.0f + ControlMetrics::SliderHeight + 32.0f +
                                      static_cast<float>(RowsNeeded) * ControlMetrics::ChipHeight +
                                      static_cast<float>(RowsNeeded - 1u) * 12.0f;
            const float SheetHeight = ControlMetrics::SectionPadding * 2.0f + std::max(LeftHeight, 100.0f);
            const Rectangle2D Content = ControlAppearance::ConstructSectionContainer(Surface, { LeftX, CursorY, BodyWidth, SheetHeight }, Radius, Opacity);
            const float TopAlignedY   = Content.Y + (Content.Height - LeftHeight) * 0.5f;

            char SizeDigits[16]{};
            std::snprintf(SizeDigits, sizeof(SizeDigits), "%d", static_cast<int>(std::lround(DraftAppearance.SlotSize[Slot])));

            Surface.PushTypeface(ResolveDraftFace(FontWeightClass::Medium));
            Surface.Text({ Content.X, TopAlignedY + 2.0f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), ThemeRepository::QueryTypeSlotLabel(Slot));
            Surface.PopTypeface();

            const float SliderRowY = TopAlignedY + 44.0f - (ControlMetrics::ControlHeight - ControlMetrics::SliderHeight) * 0.5f;
            ScalarSlider::ConstructReadoutCapsule(Surface, Content.X, SliderRowY, SizeDigits, "px", Opacity);

            const Rectangle2D SlotSliderRect{
                Content.X + ControlMetrics::ReadoutCapsuleWidth + ControlMetrics::RowGap,
                SliderRowY,
                LeftColumnWidth - ControlMetrics::ReadoutCapsuleWidth - ControlMetrics::RowGap,
                ControlMetrics::ControlHeight,
            };
            float                SlotPixels  = DraftAppearance.SlotSize[Slot];
            const int            SliderToken = 10 + static_cast<int>(Slot);
            const ControlContact Hit         = ScalarSlider::Construct(
                Surface,
                SlotSliderRect,
                8.0f,
                72.0f,
                SlotPixels,
                ActiveSliderOrdinal == SliderToken,
                Pointer,
                SlotPixels,
                Opacity);

            if (Hit.Pressed)
            {
                ActiveSliderOrdinal = SliderToken;
            }
            if (ActiveSliderOrdinal == SliderToken && Pointer.Down)
            {
                DraftAppearance.SlotSize[Slot] = std::round(SlotPixels);
            }

            float ChipX = Content.X;
            float ChipY = TopAlignedY + 44.0f + ControlMetrics::SliderHeight + 32.0f;
            for (uint32_t ChipIndex = 0u; ChipIndex < ChipCount; ++ChipIndex)
            {
                if (ChipX > Content.X && (ChipX - Content.X) + ChipWidths[ChipIndex] > LeftColumnWidth)
                {
                    ChipX = Content.X;
                    ChipY += ControlMetrics::ChipHeight + 12.0f;
                }
                const Rectangle2D ChipBounds{ ChipX, ChipY, ChipWidths[ChipIndex], ControlMetrics::ChipHeight };
                if (CapsuleButton::ConstructChip(
                        Surface,
                        ChipBounds,
                        TypographyRepository::WeightLabel(AvailableChips[ChipIndex]),
                        DraftAppearance.SlotWeight[Slot] == AvailableChips[ChipIndex],
                        Pointer,
                        Opacity)
                        .Clicked)
                {
                    DraftAppearance.SlotWeight[Slot] = AvailableChips[ChipIndex];
                }
                ChipX += ChipWidths[ChipIndex] + 12.0f;
            }

            const Rectangle2D PreviewWell{
                Content.X + LeftColumnWidth + 32.0f,
                Content.Y,
                std::max(40.0f, Content.Width - LeftColumnWidth - 32.0f),
                Content.Height,
            };
            Surface.FillRectangle(PreviewWell, SubtleWash.Faded(Opacity).ToRgba8(), Radius * 0.7f);
            ControlAppearance::ConstructRoundedOutline(Surface, PreviewWell, SubtleRing.Faded(Opacity), Radius * 0.7f);

            Surface.PushClip({ PreviewWell.X + 24.0f, PreviewWell.Y, std::max(1.0f, PreviewWell.Width - 48.0f), PreviewWell.Height });
            Surface.PushTypeface(ResolveDraftFace(DraftAppearance.SlotWeight[Slot]));
            const float LineHeight = Surface.MeasureText(DraftAppearance.SlotSize[Slot], "Ag").Y;
            Surface.Text(
                { PreviewWell.X + 24.0f, PreviewWell.Center().Y - LineHeight * 0.5f },
                DraftAppearance.SlotSize[Slot],
                Colors.Text.Faded(Opacity).ToRgba8(),
                ThemeRepository::QueryTypeSlotSample(Slot));
            Surface.PopTypeface();
            Surface.PopClip();

            CursorY += SheetHeight + 32.0f;
        }
    }

    if (Pointer.Released || !Pointer.Down)
    {
        ActiveSliderOrdinal = -1;
    }
    return (CursorY + ScrollY) - BodyBounds.Y;
}

float ControlCentrePanel::ConstructAppearanceThemeBody(
    PixelSpace&           Surface,
    Rectangle2D           BodyBounds,
    float                 ScrollY,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    const ControlColorSet& Colors    = ControlAppearance::Colors();
    const float            Radius    = std::clamp(DraftAppearance.CornerRadius, 0.0f, 32.0f);
    const float            LeftX     = BodyBounds.X;
    const float            BodyWidth = BodyBounds.Width;
    float                  CursorY   = BodyBounds.Y - ScrollY;

    // ① Color Scheme (4x2 miniature window tiles)
    {
        const float InnerWidth   = BodyWidth - ControlMetrics::SectionPadding * 2.0f;
        const float TileWidth    = (InnerWidth - 3.0f * 24.0f) / 4.0f;
        const float TileHeight   = TileWidth * 0.75f;
        const float CellHeight   = TileHeight + 28.0f;
        const float GridHeight   = CellHeight * 2.0f + 32.0f;
        const float HeaderHeight = 64.0f;
        const float BoxHeight    = ControlMetrics::SectionPadding * 2.0f + HeaderHeight + GridHeight;

        const Rectangle2D Content = ControlAppearance::ConstructSectionContainer(Surface, { LeftX, CursorY, BodyWidth, BoxHeight }, Radius, Opacity);
        ControlAppearance::ConstructSectionHeading(
            Surface,
            Content.X,
            Content.Y,
            Content.Width,
            "Color Scheme",
            "Choose the overall look of the interface",
            Colors.Text,
            Colors.TextDim,
            Opacity);

        for (uint32_t Ordinal = 0u; Ordinal < 8u; ++Ordinal)
        {
            const uint32_t    Column = Ordinal % 4u;
            const uint32_t    Row    = Ordinal / 4u;
            const ThemePreset Preset = ThemePreviewTile::PresetAtDisplayOrder(Ordinal);
            const Rectangle2D TileBounds{
                Content.X + static_cast<float>(Column) * (TileWidth + 24.0f),
                Content.Y + HeaderHeight + static_cast<float>(Row) * (CellHeight + 32.0f),
                TileWidth,
                TileHeight,
            };
            if (ThemePreviewTile::Construct(Surface, TileBounds, Preset, DraftAppearance.Theme == Preset, Radius, Pointer, Opacity).Clicked)
            {
                DraftAppearance.Theme = Preset;
            }
        }
        CursorY += BoxHeight + ControlMetrics::SectionGap;
    }

    // ② Corner Radius (0..32 px)
    {
        const float       HeaderHeight = 64.0f;
        const float       BoxHeight    = ControlMetrics::SectionPadding * 2.0f + HeaderHeight + ControlMetrics::ControlHeight;
        const Rectangle2D Content      = ControlAppearance::ConstructSectionContainer(Surface, { LeftX, CursorY, BodyWidth, BoxHeight }, Radius, Opacity);
        ControlAppearance::ConstructSectionHeading(
            Surface,
            Content.X,
            Content.Y,
            Content.Width,
            "Corner Radius",
            "Adjust the roundness of UI corners",
            Colors.Text,
            Colors.TextDim,
            Opacity);

        char RadiusDigits[16]{};
        std::snprintf(RadiusDigits, sizeof(RadiusDigits), "%d", static_cast<int>(std::lround(DraftAppearance.CornerRadius)));

        float                UpdatedRadius = DraftAppearance.CornerRadius;
        const ControlContact Hit           = ScalarSlider::ConstructWithReadout(
            Surface,
            { Content.X, Content.Y + HeaderHeight, Content.Width, ControlMetrics::ControlHeight },
            RadiusDigits,
            "px",
            0.0f,
            32.0f,
            UpdatedRadius,
            ActiveSliderOrdinal == 2,
            Pointer,
            UpdatedRadius,
            Opacity);

        if (Hit.Pressed)
        {
            ActiveSliderOrdinal = 2;
        }
        if (ActiveSliderOrdinal == 2 && Pointer.Down)
        {
            DraftAppearance.CornerRadius = std::round(UpdatedRadius);
        }
        CursorY += BoxHeight + ControlMetrics::SectionGap;
    }

    // ③ Accent Color (10 circular swatches)
    {
        const float       HeaderHeight = 64.0f;
        const float       BoxHeight    = ControlMetrics::SectionPadding * 2.0f + HeaderHeight + ControlMetrics::SwatchDiameter + 8.0f;
        const Rectangle2D Content      = ControlAppearance::ConstructSectionContainer(Surface, { LeftX, CursorY, BodyWidth, BoxHeight }, Radius, Opacity);
        ControlAppearance::ConstructSectionHeading(
            Surface,
            Content.X,
            Content.Y,
            Content.Width,
            "Accent Color",
            "Highlight colour for active controls",
            Colors.Text,
            Colors.TextDim,
            Opacity);

        const float CenterY = Content.Y + HeaderHeight + 20.0f;
        for (uint32_t Index = 0u; Index < 10u; ++Index)
        {
            const float CenterX = Content.X + 16.0f + static_cast<float>(Index) * (ControlMetrics::SwatchDiameter + 12.0f);
            const auto  Preset  = static_cast<AccentPreset>(Index);
            if (ThemePreviewTile::ConstructColorSwatch(
                    Surface,
                    CenterX,
                    CenterY,
                    ThemeRepository::QueryAccentColor(Preset),
                    DraftAppearance.Accent == Preset,
                    true,
                    Pointer,
                    Opacity)
                    .Clicked)
            {
                DraftAppearance.Accent = Preset;
            }
        }
        CursorY += BoxHeight + ControlMetrics::SectionGap;
    }

    // ④ Semantic Colors (Warning, Success, Notice, Caution)
    {
        const float       HeaderHeight = 64.0f;
        const float       RowHeight    = 100.0f;
        const float       RowStride    = 65.0f;
        const float       BoxHeight    = ControlMetrics::SectionPadding * 2.0f + HeaderHeight + RowHeight * 4.0f + RowStride * 3.0f;
        const Rectangle2D Content      = ControlAppearance::ConstructSectionContainer(Surface, { LeftX, CursorY, BodyWidth, BoxHeight }, Radius, Opacity);
        ControlAppearance::ConstructSectionHeading(
            Surface,
            Content.X,
            Content.Y,
            Content.Width,
            "Semantic Colors",
            "Tones used by alerts, dialogues and toasts",
            Colors.Text,
            Colors.TextDim,
            Opacity);

        struct SemanticRowEntry
        {
            const char* Heading;        // [-] - Semantic row label
            const char* Message;        // [-] - Sample alert message
            ControlIcon Icon;           // [-] - Sample alert icon
            uint32_t*   SelectedSwatch; // [-] - Mutable swatch index
        };
        SemanticRowEntry Entries[4] = {
            { "Warning", "Warning Message", ControlIcon::TriangleAlert, &DraftAppearance.WarningSwatch },
            { "Success", "Success Message", ControlIcon::CircleCheck,   &DraftAppearance.SuccessSwatch },
            { "Notice",  "Notice Message",  ControlIcon::CircleNotice,  &DraftAppearance.NoticeSwatch  },
            { "Caution", "Caution Message", ControlIcon::OctagonAlert,  &DraftAppearance.CautionSwatch },
        };

        const ColorQuad WellFill = Colors.LightSurface
                                       ? ColorQuad{ 0.0f, 0.0f, 0.0f, 0.06f }
                                       : ColorQuad{ 1.0f, 1.0f, 1.0f, 0.10f };
        float RowY = Content.Y + HeaderHeight;
        for (uint32_t RowIndex = 0u; RowIndex < 4u; ++RowIndex)
        {
            const float LeftWidth  = 300.0f;
            const float LeftHeight = 20.0f + 24.0f + ControlMetrics::SwatchDiameter;
            const float LeftY      = RowY + (RowHeight - LeftHeight) * 0.5f;

            Surface.Text({ Content.X, LeftY + 2.0f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), Entries[RowIndex].Heading);
            for (uint32_t Swatch = 0u; Swatch < 4u; ++Swatch)
            {
                const float CenterX = Content.X + 16.0f + static_cast<float>(Swatch) * (ControlMetrics::SwatchDiameter + 12.0f);
                const float CenterY = LeftY + 60.0f;
                if (ThemePreviewTile::ConstructColorSwatch(
                        Surface,
                        CenterX,
                        CenterY,
                        ThemeRepository::QuerySemanticColor(RowIndex, Swatch),
                        *(Entries[RowIndex].SelectedSwatch) == Swatch,
                        false,
                        Pointer,
                        Opacity)
                        .Clicked)
                {
                    *(Entries[RowIndex].SelectedSwatch) = Swatch;
                }
            }

            const Rectangle2D PreviewBox{
                Content.X + LeftWidth + 32.0f,
                RowY,
                std::max(80.0f, Content.Width - LeftWidth - 32.0f),
                RowHeight,
            };
            Surface.FillRectangle(PreviewBox, WellFill.Faded(Opacity).ToRgba8(), Radius * 0.7f);
            ControlAppearance::ConstructRoundedOutline(Surface, PreviewBox, Colors.Stroke.Faded(Opacity), Radius * 0.7f);

            const ColorQuad Tone = ThemeRepository::QuerySemanticColor(RowIndex, *(Entries[RowIndex].SelectedSwatch));
            ControlAppearance::ConstructIcon(
                Surface,
                PreviewBox.X + 24.0f,
                PreviewBox.Y + (RowHeight - 20.0f) * 0.5f,
                20.0f,
                Tone.Faded(Opacity),
                Entries[RowIndex].Icon,
                2.0f);
            const Point2D MessageExtent = Surface.MeasureText(18.0f, Entries[RowIndex].Message);
            Surface.Text(
                { PreviewBox.X + 52.0f, PreviewBox.Y + (RowHeight - MessageExtent.Y) * 0.5f },
                18.0f,
                Tone.Faded(Opacity).ToRgba8(),
                Entries[RowIndex].Message);

            if (RowIndex < 3u)
            {
                ControlAppearance::ConstructHairline(Surface, Content.X, RowY + RowHeight + 32.0f, Content.Width, Colors.Divider.Faded(Opacity));
            }
            RowY += RowHeight + RowStride;
        }
        CursorY += BoxHeight;
    }

    if (Pointer.Released || !Pointer.Down)
    {
        ActiveSliderOrdinal = -1;
    }
    return (CursorY + ScrollY) - BodyBounds.Y + 24.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                        INPUT, NOTIFICATIONS & WORKSPACE BODIES
//------------------------------------------------------------------------------------------------------------------------

float ControlCentrePanel::ConstructInputSettingsBody(
    PixelSpace&           Surface,
    Rectangle2D           BodyBounds,
    float                 ScrollY,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    const ControlColorSet& Colors  = ControlAppearance::Colors();
    const float            LeftX   = BodyBounds.X + 16.0f;
    const float            Width   = std::max(1.0f, std::min(BodyBounds.Width - 32.0f, 768.0f));
    float                  CursorY = BodyBounds.Y - ScrollY;

    ControlPointer LocalPointer = Pointer;
    if (InputProfileSelectorOpen)
    {
        LocalPointer.Enabled = false;
    }

    auto ConstructFieldBox = [&](Rectangle2D Box)
    {
        Surface.FillRectangle(Box, Colors.Field.Faded(Opacity).ToRgba8(), 8.0f);
        ControlAppearance::ConstructRoundedOutline(Surface, Box, Colors.StrokeStrong.Faded(Opacity), 8.0f);
    };

    auto LayoutFormRow = [&](const char* Label) -> Rectangle2D
    {
        Surface.Text({ LeftX, CursorY + 12.0f }, 13.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), Label);
        if (Width < 500.0f)
        {
            CursorY += 32.0f;
            return { LeftX, CursorY, Width, 40.0f };
        }
        return { LeftX + 156.0f, CursorY, Width - 156.0f, 40.0f };
    };

    auto ConstructFormSection = [&](const char* Heading)
    {
        CursorY += 24.0f;
        ControlAppearance::ConstructHairline(Surface, LeftX, CursorY, Width, Colors.Divider.Faded(Opacity));
        CursorY += 24.0f;
        Surface.Text({ LeftX, CursorY + 4.0f }, 14.5f, Colors.Text.Faded(Opacity).ToRgba8(), Heading);
        CursorY += 40.0f;
    };

    InputProfileBounds = LayoutFormRow("Shortcut profile");
    ConstructFieldBox(InputProfileBounds);
    ControlAppearance::TextLeading(
        Surface,
        InputProfileBounds,
        16.0f,
        Colors.Text.Faded(Opacity),
        DraftInput.CustomShortcutsEnabled ? "Custom" : "Editor Default",
        13.0f);
    ControlAppearance::ConstructIconCentered(
        Surface,
        { InputProfileBounds.Right() - 32.0f, InputProfileBounds.Y, 24.0f, 40.0f },
        16.0f,
        Colors.TextDim.Faded(Opacity),
        ControlIcon::ChevronDown);
    if (ControlAppearance::QueryPointerOver(InputProfileBounds, LocalPointer) && LocalPointer.Released)
    {
        InputProfileSelectorOpen = true;
        CaptureShortcutSlot      = -1;
    }
    CursorY += 52.0f;

    const Rectangle2D SensitivityRow = LayoutFormRow("Mouse Sensitivity");
    char              SensitivityText[16]{};
    std::snprintf(SensitivityText, sizeof(SensitivityText), "%.0f", static_cast<double>(DraftInput.MouseSensitivity));

    float                UpdatedSensitivity = DraftInput.MouseSensitivity;
    const ControlContact SensitivityHit     = ScalarSlider::ConstructWithReadout(
        Surface,
        SensitivityRow,
        SensitivityText,
        "%",
        0.0f,
        100.0f,
        UpdatedSensitivity,
        ActiveSliderOrdinal == 30,
        LocalPointer,
        UpdatedSensitivity,
        Opacity);

    if (SensitivityHit.Pressed)
    {
        ActiveSliderOrdinal = 30;
    }
    if (ActiveSliderOrdinal == 30 && Pointer.Down)
    {
        DraftInput.MouseSensitivity = std::round(UpdatedSensitivity);
    }
    if (Pointer.Released || !Pointer.Down)
    {
        ActiveSliderOrdinal = -1;
    }
    CursorY += 52.0f;

    ConstructFormSection("Custom Shortcuts");
    if (ExclusiveSelector::Construct(
            Surface,
            LeftX + Width - ControlMetrics::SwitchWidth,
            CursorY - 40.0f,
            DraftInput.CustomShortcutsEnabled,
            LocalPointer,
            Opacity)
            .Clicked)
    {
        DraftInput.CustomShortcutsEnabled = !DraftInput.CustomShortcutsEnabled;
        CaptureShortcutSlot               = -1;
        StatusDiagnostic.clear();
    }

    for (uint32_t Slot = 0u; Slot < InputPreferences::ShortcutCount; ++Slot)
    {
        const auto        Command = static_cast<ShortcutCommand>(Slot);
        const Rectangle2D Field   = LayoutFormRow(InputSettingsRepository::QueryCommandLabel(Command));
        ConstructFieldBox(Field);

        const bool        Capturing = (CaptureShortcutSlot == static_cast<int>(Slot));
        const std::string ChordText = Capturing
                                          ? "Press a key (Esc cancels)"
                                          : InputSettingsRepository::FormatChordLabel(DraftInput.QueryActiveShortcut(Command));

        Surface.PushClip(Field);
        ControlAppearance::TextLeading(
            Surface,
            Field,
            16.0f,
            (DraftInput.CustomShortcutsEnabled ? Colors.Text : Colors.TextFaint).Faded(Opacity),
            ChordText.c_str(),
            13.0f);
        Surface.PopClip();

        if (Capturing)
        {
            ControlAppearance::ConstructRoundedOutline(Surface, Field, Colors.Accent.Faded(Opacity), 8.0f, 1.5f);
        }
        if (LocalPointer.Pressed && ControlAppearance::QueryPointerOver(Field, LocalPointer))
        {
            PressedShortcutSlot = static_cast<int>(Slot);
        }
        if (DraftInput.CustomShortcutsEnabled &&
            PressedShortcutSlot == static_cast<int>(Slot) &&
            ControlAppearance::QueryPointerOver(Field, LocalPointer) &&
            LocalPointer.Released)
        {
            CaptureShortcutSlot = static_cast<int>(Slot);
            StatusDiagnostic.clear();
        }
        CursorY += 52.0f;
    }
    if (!Pointer.Down)
    {
        PressedShortcutSlot = -1;
    }

    ConstructFormSection("Advanced Controls");
    if (ExclusiveSelector::Construct(
            Surface,
            LeftX + Width - ControlMetrics::SwitchWidth,
            CursorY - 40.0f,
            DraftInput.AdvancedControlsEnabled,
            LocalPointer,
            Opacity)
            .Clicked)
    {
        DraftInput.AdvancedControlsEnabled = !DraftInput.AdvancedControlsEnabled;
    }

    const Rectangle2D InvertRow      = LayoutFormRow("Invert Y-Axis");
    ControlPointer    InvertPointer  = LocalPointer;
    InvertPointer.Enabled            = InvertPointer.Enabled && DraftInput.AdvancedControlsEnabled;
    if (ExclusiveSelector::Construct(
            Surface,
            InvertRow.Right() - ControlMetrics::SwitchWidth,
            InvertRow.Y + 7.0f,
            DraftInput.InvertedPitchEnabled,
            InvertPointer,
            Opacity * (DraftInput.AdvancedControlsEnabled ? 1.0f : 0.40f))
            .Clicked)
    {
        DraftInput.InvertedPitchEnabled = !DraftInput.InvertedPitchEnabled;
    }
    CursorY += 56.0f;

    char PreviewLine[160]{};
    std::snprintf(
        PreviewLine,
        sizeof(PreviewLine),
        "Applied pointer response: dx %.1f / dy %.1f (UI preview)",
        static_cast<double>(PreviewPointerDelta.X),
        static_cast<double>(PreviewPointerDelta.Y));
    Surface.Text({ LeftX, CursorY }, 12.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), PreviewLine);
    CursorY += 24.0f;
    Surface.Text({ LeftX, CursorY }, 11.0f, Colors.TextFaint.Faded(Opacity).ToRgba8(), "Scene camera/tools are not hosted; tray gestures stay 1:1.");
    CursorY += 24.0f;

    return (CursorY + ScrollY) - BodyBounds.Y;
}

void ControlCentrePanel::ConstructInputProfileOverlay(PixelSpace& Surface, const ControlPointer& Pointer, float Opacity)
{
    if (!InputProfileSelectorOpen)
    {
        return;
    }

    const ControlColorSet& Colors      = ControlAppearance::Colors();
    const Rectangle2D      OverlayRect{ InputProfileBounds.X, InputProfileBounds.Bottom() + 6.0f, InputProfileBounds.Width, 88.0f };
    Surface.FillRectangle(OverlayRect, Colors.Field.Faded(Opacity).ToRgba8(), 8.0f);
    ControlAppearance::ConstructRoundedOutline(Surface, OverlayRect, Colors.StrokeStrong.Faded(Opacity), 8.0f);

    constexpr const char* ProfileLabels[2] = { "Editor Default", "Custom" };
    for (uint32_t Index = 0u; Index < 2u; ++Index)
    {
        const Rectangle2D Row{
            InputProfileBounds.X + 4.0f,
            InputProfileBounds.Bottom() + 10.0f + static_cast<float>(Index) * 40.0f,
            InputProfileBounds.Width - 8.0f,
            40.0f,
        };
        const bool Hover = ControlAppearance::QueryPointerOver(Row, Pointer);
        if (Hover)
        {
            Surface.FillRectangle(Row, Colors.Selected.Faded(Opacity).ToRgba8(), 4.0f);
        }
        ControlAppearance::TextLeading(Surface, Row, 12.0f, Colors.Text.Faded(Opacity), ProfileLabels[Index], 13.0f);
        if (Hover && Pointer.Released)
        {
            DraftInput.CustomShortcutsEnabled = (Index == 1u);
            InputProfileSelectorOpen          = false;
            return;
        }
    }

    if (Pointer.Pressed &&
        !OverlayRect.Contains({ Pointer.X, Pointer.Y }) &&
        !InputProfileBounds.Contains({ Pointer.X, Pointer.Y }))
    {
        InputProfileSelectorOpen = false;
    }
}

float ControlCentrePanel::ConstructNotificationsBody(
    PixelSpace&           Surface,
    Rectangle2D           BodyBounds,
    float                 ScrollY,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    const ControlColorSet& Colors  = ControlAppearance::Colors();
    const float            LeftX   = BodyBounds.X + 16.0f;
    const float            Width   = std::min(BodyBounds.Width - 32.0f, 768.0f);
    float                  CursorY = BodyBounds.Y - ScrollY;

    auto ConstructAlertToggleRow = [&](const char* Label, bool& ToggleField, bool LastInGroup)
    {
        const Point2D TextExtent = Surface.MeasureText(14.0f, Label);
        Surface.Text({ LeftX, CursorY + (56.0f - TextExtent.Y) * 0.5f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), Label);
        const Rectangle2D SwitchRect{ LeftX + Width - 48.0f, CursorY + 16.0f, 48.0f, 24.0f };
        if (ExclusiveSelector::ConstructCompact(Surface, SwitchRect, ToggleField, Pointer, Opacity).Clicked)
        {
            ToggleField = !ToggleField;
        }
        CursorY += 56.0f;
        if (!LastInGroup)
        {
            ControlAppearance::ConstructHairline(Surface, LeftX, CursorY, Width, Colors.Divider.Faded(Opacity));
        }
    };

    ConstructAlertToggleRow("Show FPS Overlay", DraftNotifications.RateOverlayEnabled, true);

    CursorY += 32.0f;
    Surface.Text({ LeftX, CursorY }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), "Interface alerts");
    CursorY += Surface.MeasureText(14.0f, "Interface alerts").Y + 16.0f;
    ConstructAlertToggleRow("Preferences Applied", DraftNotifications.AppliedAlertsEnabled, false);
    ConstructAlertToggleRow("Preference Errors", DraftNotifications.ErrorAlertsEnabled, true);

    CursorY += 32.0f;
    Surface.Text({ LeftX, CursorY }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), "Toasts");
    CursorY += Surface.MeasureText(14.0f, "Toasts").Y + 16.0f;

    const Point2D HoldLabelExtent = Surface.MeasureText(14.0f, "Hold Duration");
    Surface.Text({ LeftX, CursorY + (56.0f - HoldLabelExtent.Y) * 0.5f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), "Hold Duration");

    char HoldText[16]{};
    std::snprintf(HoldText, sizeof(HoldText), "%.1f", static_cast<double>(DraftNotifications.AlertHoldSeconds));

    const float          RowTop      = CursorY + (56.0f - ControlMetrics::ControlHeight) * 0.5f;
    float                HoldSeconds = DraftNotifications.AlertHoldSeconds;
    const ControlContact Hit         = ScalarSlider::ConstructWithReadout(
        Surface,
        { LeftX + 160.0f, RowTop, std::max(120.0f, Width - 160.0f), ControlMetrics::ControlHeight },
        HoldText,
        "s",
        1.0f,
        10.0f,
        HoldSeconds,
        ActiveSliderOrdinal == 40,
        Pointer,
        HoldSeconds,
        Opacity);

    if (Hit.Pressed)
    {
        ActiveSliderOrdinal = 40;
    }
    if (ActiveSliderOrdinal == 40 && Pointer.Down)
    {
        DraftNotifications.AlertHoldSeconds = std::round(HoldSeconds * 2.0f) * 0.5f;
    }
    if (Pointer.Released || !Pointer.Down)
    {
        ActiveSliderOrdinal = -1;
    }

    CursorY += 56.0f;
    return (CursorY + ScrollY) - BodyBounds.Y + 24.0f;
}

float ControlCentrePanel::ConstructWorkspaceBody(
    PixelSpace&           Surface,
    Rectangle2D           BodyBounds,
    float                 ScrollY,
    const ControlPointer& Pointer,
    float                 Opacity)
{
    const ControlColorSet& Colors  = ControlAppearance::Colors();
    const float            LeftX   = BodyBounds.X + 16.0f;
    const float            Width   = std::min(BodyBounds.Width - 32.0f, 768.0f);
    float                  CursorY = BodyBounds.Y - ScrollY;

    Surface.Text({ LeftX, CursorY + 6.0f }, 14.0f, Colors.Text.Faded(Opacity).ToRgba8(), "Allow detached OS windows");
    if (ExclusiveSelector::Construct(
            Surface,
            LeftX + Width - ControlMetrics::SwitchWidth,
            CursorY,
            DetachedWindowsAllowed,
            Pointer,
            Opacity)
            .Clicked)
    {
        ConfigureDetachedWindowsAllowed(!DetachedWindowsAllowed);
    }

    CursorY += 44.0f;
    Surface.Text({ LeftX, CursorY }, 12.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "Disabled by default.");
    CursorY += 20.0f;
    Surface.Text({ LeftX, CursorY }, 12.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "Optional for multiple monitors.");
    CursorY += 20.0f;
    Surface.Text({ LeftX, CursorY }, 12.0f, Colors.TextDim.Faded(Opacity).ToRgba8(), "Off returns panels to the main window.");
    CursorY += 40.0f;

    const ButtonDescriptor ResetButton{ "Reset saved layout", ButtonTone::Secondary, 13.5f, 22.0f, false, {} };
    const float            ResetWidth = CapsuleButton::MeasureWidth(Surface, ResetButton);
    if (CapsuleButton::Construct(Surface, { LeftX, CursorY, ResetWidth, 40.0f }, ResetButton, Pointer, Opacity).Clicked)
    {
        if (ResetLayoutCallback)
        {
            ResetLayoutCallback();
        }
        Notifications.Broadcast(
            NotificationSeverity::Notice,
            "Workspace layout reset",
            "Returned docked panes to the default workspace split.",
            AppliedNotifications.AlertHoldSeconds,
            true);
    }

    CursorY += 56.0f;
    return (CursorY + ScrollY) - BodyBounds.Y;
}

} // namespace Frontier
