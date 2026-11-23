/*==========================================================================================================================================
                                                        EDITORWORKSPACE.CPP
==========================================================================================================================================*/
// 🧩 Host the original Content Browser and optional Notification Stream through callbacks into their original owners.

#include "EditorWorkspace.h"
#include <cmath>
#include <imgui.h>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace Frontier
{
EditorWorkspace::EditorWorkspace(ControlCentrePanel &ControlCentre)
    : Centre(ControlCentre), DockableBrowser(ControlCentre.QueryDockableContentBrowser())
{
    if (!Panels.Register({"content", "Content Browser",
                          [&]()
                          {
                              Centre.QueryContentBrowser().ConstructHostedInterface(
                                  Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f,
                                  (!Centre.QueryQuickOpen() ||
                                   ImGui::GetWindowViewport() != ImGui::GetMainViewport()) &&
                                      !Centre.QueryConfirmation().QueryOpen());
                          },
                          DockableBrowser, [&]() { Centre.QueryContentBrowser().SuspendInteraction(); },
                          [&]() { return Centre.QueryDockableContentBrowser(); }}) ||
        !Panels.Register(
            {"notifications", "Notification Stream", [&]() { Centre.ConstructHostedNotificationStream(); }, false}) ||
        !Panels.Register({"viewport", "Viewport", [&]() { ConstructViewport(); }, true}))
        throw std::runtime_error("Panel registration refused.");
    // 📝 The top bar owns the first 52 unscaled pixels; the dockspace starts below it.
    Panels.ConfigureWorkspaceInsets(52, 0);
    ImGui::GetIO().IniFilename = nullptr;
    ResetRevision              = Centre.QueryLayoutResetRevision();
    const auto &Path           = Centre.QueryPersonalProjectPath();
    if (Path.extension() != ".projectspace")
        ConfigureLayoutDiagnostic("Layout is session-only: no personal projectspace.");
    if (Path.extension() == ".projectspace" && std::filesystem::exists(Path))
    {
        ProjectSpaceDocument Document;
        std::string          Message;
        if (!ProjectSpaceCodec::Decode(Path, Document, Message))
        {
            LayoutBlocked = true;
            ConfigureLayoutDiagnostic("Layout read refused: " + Message);
        }
        else if (const auto Layout = Document.Sections["workspace"]["layout"]; Layout)
        {
            if (Layout.is_table() && Panels.ConfigureLayout(*Layout.as_table(), Message))
            {
                LayoutRestored = true;
                if (!DockableBrowser)
                    Centre.QueryContentBrowser().ConfigureOpen(
                        (*Layout.as_table())["browser_drawer_open"].value_or(false));
            }
            else
            {
                LayoutBlocked = true;
                ConfigureLayoutDiagnostic("Saved layout refused; defaults active. Reset saved layout to replace it.");
            }
        }
    }
}

EditorWorkspace::~EditorWorkspace()
{
    try
    {
        if (!FinalizeLayout() && !LayoutDiagnostic.empty())
            std::cerr << LayoutDiagnostic << '\n';
    }
    catch (const std::exception &Failure)
    {
        std::cerr << "Layout save failed: " << Failure.what() << '\n';
    }
    Centre.ConfigureDockedPanels(false, false);
    Centre.QueryContentBrowser().SuspendInteraction();
}

bool EditorWorkspace::ConfigurePlatform() noexcept
{
    Panels.ConfigureDetachedWindows(Centre.QueryDetachedWindows());
    return Panels.ConfigurePlatform();
}

void EditorWorkspace::ConstructInterface()
{
    if (ResetRevision != Centre.QueryLayoutResetRevision())
    {
        ResetRevision = Centre.QueryLayoutResetRevision();
        Panels.ResetLayout();
        (void)Panels.ConfigureOpen("content", Centre.QueryDockableContentBrowser());
        (void)Panels.ConfigureOpen("notifications", false);
        (void)Panels.ConfigureOpen("viewport", true);
        LayoutBlocked = false;
        SavedLayout.clear();
        ObservedLayout.clear();
        ObservedSnapshot.clear();
        PersistenceFrames = 0;
        RetrySeconds = StableSeconds = 0;
        ConfigureLayoutDiagnostic("Layout reset; saves automatically.");
    }
    if (DockableBrowser != Centre.QueryDockableContentBrowser())
    {
        const bool Visible = Panels.QueryOpen("content");
        DockableBrowser    = Centre.QueryDockableContentBrowser();
        Centre.QueryContentBrowser().SuspendInteraction();
        (void)Panels.ConfigureOpen("content", DockableBrowser);
        Centre.QueryContentBrowser().ConfigureOpen(!DockableBrowser && Visible);
    }
    const float Scale = Centre.QueryAppearance().QueryApplied().InterfaceScale / 100.0f;
    Centre.ConfigureDockedPanels(DockableBrowser, Panels.QueryOpen("notifications"));
    ConstructTopBar(Scale);
    Panels.ConstructInterface(Scale);
    Rendered = true;
    Centre.ConfigureDockedPanels(DockableBrowser, Panels.QueryOpen("notifications"));
}

void EditorWorkspace::ConstructTopBar(float Scale)
{
    const auto *Main = ImGui::GetMainViewport();
    if (Main->WorkSize.x <= 0 || Main->WorkSize.y <= 0) return;
    ImGui::SetNextWindowPos(Main->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(Main->WorkSize.x, 52 * Scale));
    ImGui::SetNextWindowViewport(Main->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12 * Scale, 0));
    ImGui::Begin("Frontier editor top bar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus);
    const float CentreY = (52 * Scale - ImGui::GetFrameHeight()) * .5f;
    ImGui::SetCursorPosY(CentreY);
    if (ImGui::Button("< Back"))
    {
        if (EvaluateReturnToBrowser) EvaluateReturnToBrowser();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Return to the Project Browser");
    ImGui::SameLine();
    ImGui::SetCursorPosY(CentreY);
    ImGui::TextUnformatted(Project.Name.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosY(CentreY);
    ImGui::TextDisabled("%zu levels · %zu catalogue entries", Project.Levels, Project.Catalogue);
    const char *Actions[] = {"Control centre", "Theme", "Settings"};
    float ActionsWidth = 0;
    for (const auto *Action : Actions)
        ActionsWidth += ImGui::CalcTextSize(Action).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(Main->WorkSize.x - ActionsWidth - 12 * Scale);
    ImGui::SetCursorPosY(CentreY);
    if (ImGui::Button(Actions[0])) Centre.ActivateSettingsPage(ControlCentrePageCategory::QuickControls);
    ImGui::SameLine();
    ImGui::SetCursorPosY(CentreY);
    if (ImGui::Button(Actions[1])) Centre.ActivatePage(AppearancePageCategory::Theme);
    ImGui::SameLine();
    ImGui::SetCursorPosY(CentreY);
    if (ImGui::Button(Actions[2])) Centre.ActivateSettingsPage(ControlCentrePageCategory::Settings);
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorWorkspace::ConstructViewport()
{
    ImGui::TextDisabled("SCENE CANVAS");
    ImGui::TextWrapped("Native scene and document rendering are not connected yet. "
                       "The Content Browser tab holds this project's real files; appearance, input and "
                       "notification settings live behind Control centre, Theme and Settings above.");
    ImGui::Spacing();
    ImGui::Separator();
    const auto Row = [](const char *Label, const std::string &Value)
    {
        ImGui::TextDisabled("%s", Label);
        ImGui::SameLine(150);
        ImGui::TextWrapped("%s", Value.empty() ? "—" : Value.c_str());
    };
    Row("Project", Project.Name);
    Row("Location", Project.Location);
    Row("Identity", Project.Identity);
    Row("Levels", std::to_string(Project.Levels));
    Row("Content roots", std::to_string(Project.Roots));
    Row("Catalogue", std::to_string(Project.Catalogue));
    Row("Startup level", Project.StartupLevel);
}

namespace
{
std::string EncodeText(const toml::table &Layout)
{
    std::ostringstream Text;
    Text << Layout;
    return Text.str();
}
} // namespace

void EditorWorkspace::ConfigureLayoutDiagnostic(std::string Message)
{
    LayoutDiagnostic = std::move(Message);
    Centre.ConfigureLayoutDiagnostic(LayoutDiagnostic);
}

toml::table EditorWorkspace::CaptureLayout() const
{
    auto Layout = Panels.EncodeLayout();
    if (!Layout.empty())
        Layout.insert_or_assign("browser_drawer_open", Centre.QueryContentBrowser().QueryExpansionTab().QueryOpen());
    return Layout;
}

bool EditorWorkspace::FinalizeLayout()
{
    if (!Rendered || !Panels.QuerySeated() || LayoutBlocked)
        return !LayoutBlocked;
    const auto &Path = Centre.QueryPersonalProjectPath();
    if (Path.extension() != ".projectspace")
        return true;
    const auto &IO     = ImGui::GetIO();
    const auto  Layout = IO.DisplaySize.x > 0 && IO.DisplaySize.y > 0 ? CaptureLayout() : ObservedSnapshot;
    if (Layout.empty())
        return true;
    std::string Message;
    if (!PanelDockspace::ValidateLayout(Layout, Message))
    {
        ConfigureLayoutDiagnostic("Layout save refused: " + Message);
        return false;
    }
    const auto Text = EncodeText(Layout);
    if (Text == SavedLayout)
        return true;
    if (!Centre.PersistWorkspaceLayout(Layout, Message))
    {
        ConfigureLayoutDiagnostic("Layout save failed: " + Message);
        return false;
    }
    SavedLayout = Text;
    ConfigureLayoutDiagnostic("Layout saved in personal projectspace.");
    return true;
}

void EditorWorkspace::IntegrateLayoutPersistence(double ElapsedSeconds)
{
    if (!Rendered || LayoutBlocked || !std::isfinite(ElapsedSeconds) || ElapsedSeconds < 0 ||
        Centre.QueryPersonalProjectPath().extension() != ".projectspace")
        return;
    // 📝 Call after Render: tabs, splitters and platform geometry must have completed the current frame.
    if (++PersistenceFrames < 8 || ImGui::GetIO().DisplaySize.x <= 0 || ImGui::GetIO().DisplaySize.y <= 0)
        return;
    const auto Snapshot = CaptureLayout();
    const auto Current  = EncodeText(Snapshot);
    RetrySeconds        = std::max(0.0, RetrySeconds - ElapsedSeconds);
    if (Current != ObservedLayout)
    {
        ObservedLayout   = Current;
        ObservedSnapshot = Snapshot;
        StableSeconds    = 0;
        return;
    }
    StableSeconds += std::min(ElapsedSeconds, .25);
    if (Current != SavedLayout && StableSeconds >= 2 && RetrySeconds <= 0 && !ImGui::IsMouseDown(0) &&
        !ImGui::IsMouseDown(1) &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
        if (!FinalizeLayout())
            RetrySeconds = 10;
}
} // namespace Frontier
