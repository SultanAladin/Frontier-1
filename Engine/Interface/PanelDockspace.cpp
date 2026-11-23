/*==========================================================================================================================================
                                                        PANELDOCKSPACE.CPP
==========================================================================================================================================*/
// 🧩 Reuse patches A–F and native docking/splitting, optional GLFW platform windows, tab reopening, and dock-back requests.

#include "PanelDockspace.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     REGISTRATION AND PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

bool PanelDockspace::Register(DockPanelSpecification Specification)
{
    if (Seated || Entries.size() >= 16 || Specification.Key.empty() || Specification.Key.size() > 64 ||
        Specification.Key.find_first_of("#\r\n") != std::string::npos || Specification.Caption.empty() ||
        Specification.Caption.size() > 80 || !Specification.ConstructContent ||
        std::any_of(Entries.begin(), Entries.end(),
                    [&](const auto &Entry) { return Entry.Specification.Key == Specification.Key; }))
        return false;
    const auto Name = Specification.Caption + "###FrontierPanel-" + Specification.Key;
    Entries.push_back({std::move(Specification), Name, 0});
    return true;
}

bool PanelDockspace::ConfigureDockspaceKey(std::string Key)
{
    if (Seated || Key.empty() || Key.size() > 80 || Key.find_first_of("#\r\n") != std::string::npos) return false;
    DockspaceKey = std::move(Key);
    return true;
}

void PanelDockspace::ConfigureWorkspaceInsets(float Top, float Bottom) noexcept
{
    if (!Seated && std::isfinite(Top) && std::isfinite(Bottom) && Top >= 0 && Bottom >= 0 && Top <= 128 && Bottom <= 128)
    {
        TopInset = Top;
        BottomInset = Bottom;
    }
}

void PanelDockspace::ConfigureTabExtent(float Width, float Height) noexcept
{
    if (std::isfinite(Width) && std::isfinite(Height))
    {
        TabWidth = std::clamp(Width, 80.f, 240.f);
        TabHeight = std::clamp(Height, 18.f, 40.f);
    }
}

bool PanelDockspace::ConfigureStackedPanels(std::string UpperKey, std::string LowerKey, float UpperHeight)
{
    const auto Contains = [&](const std::string &Key)
    {
        return std::any_of(Entries.begin(), Entries.end(), [&](const auto &Entry) { return Entry.Specification.Key == Key; });
    };
    if (Seated || UpperKey.empty() || LowerKey.empty() || UpperKey == LowerKey || !Contains(UpperKey) ||
        !Contains(LowerKey) || !std::isfinite(UpperHeight) || UpperHeight < 1.f || UpperHeight > 2048.f) return false;
    StackedUpperKey = std::move(UpperKey);
    StackedLowerKey = std::move(LowerKey);
    StackedUpperHeight = UpperHeight;
    return true;
}

bool PanelDockspace::ConfigureColumns(const std::vector<std::pair<std::string, float>> &Columns)
{
    const size_t StackedCount = StackedUpperKey.empty() ? 0 : 1;
    if (Seated || Columns.size() < 2 || Columns.size() + StackedCount != Entries.size()) return false;
    for (size_t Index = 0; Index < Columns.size(); ++Index)
    {
        const auto &[Key, Weight] = Columns[Index];
        if (!std::isfinite(Weight) || Weight <= 0 || Weight > 1 || Key == StackedUpperKey ||
            std::none_of(Entries.begin(), Entries.end(), [&](const auto &Entry) { return Entry.Specification.Key == Key; })) return false;
        for (size_t Previous = 0; Previous < Index; ++Previous)
            if (Columns[Previous].first == Key) return false;
    }
    if (!StackedLowerKey.empty() && std::none_of(Columns.begin(), Columns.end(), [&](const auto &Column)
        { return Column.first == StackedLowerKey; })) return false;
    DefaultColumns = Columns;
    return true;
}

bool PanelDockspace::ConfigureOpen(const std::string &Key, bool Open) noexcept
{
    for (auto &Entry : Entries)
        if (Entry.Specification.Key == Key)
        {
            if (Open && Entry.Specification.Available && !Entry.Specification.Available())
                return false;
            Entry.Specification.Open = Open;
            return true;
        }
    return false;
}

bool PanelDockspace::QueryOpen(const std::string &Key) const noexcept
{
    for (const auto &Entry : Entries)
        if (Entry.Specification.Key == Key)
            return Entry.Specification.Open;
    return false;
}

bool PanelDockspace::ConfigureDetached(const std::string &Key, bool Detached) noexcept
{
    if (Detached && (!DetachedEnabled || !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)))
        return false;
    for (auto &Entry : Entries)
        if (Entry.Specification.Key == Key)
        {
            if (Entry.Specification.Available && !Entry.Specification.Available())
                return false;
            Entry.Specification.Open = true;
            Entry.Placement          = Detached ? 1 : -1;
            return true;
        }
    return false;
}

void PanelDockspace::ConfigureDetachedWindows(bool Enabled) noexcept
{
    Reattach        = Reattach || (DetachedEnabled && !Enabled);
    DetachedEnabled = Enabled;
}

bool PanelDockspace::ConfigurePlatform() noexcept
{
    auto       &IO        = ImGui::GetIO();
    const auto &Platform  = ImGui::GetPlatformIO();
    const bool  Supported = (IO.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) &&
                            (IO.BackendFlags & ImGuiBackendFlags_RendererHasViewports) &&
                            Platform.Platform_CreateWindow && Platform.Platform_DestroyWindow &&
                            Platform.Renderer_CreateWindow && Platform.Renderer_DestroyWindow &&
                            Platform.Renderer_RenderWindow;
    const bool  Enabled   = DetachedEnabled && Supported;
    if (!Enabled && (IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
    {
        // 📝 Keep the main GLFW viewport allocation intact; DestroyPlatformWindows is shutdown-only.
        for (int Index = Platform.Viewports.Size - 1; Index > 0; --Index)
            ImGui::DestroyPlatformWindow(static_cast<ImGuiViewportP *>(Platform.Viewports[Index]));
        Reattach = true;
    }
    if (Enabled)
        IO.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    else
        IO.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    IO.ConfigViewportsNoDecoration = false;
    IO.ConfigDpiScaleViewports     = true;
    IO.ConfigDpiScaleFonts         = true;
    return !DetachedEnabled || Supported;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PATCHED DOCKSPACE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
void ConfigureLeafButtons(ImGuiDockNode *Region, bool &Requested)
{
    if (!Region)
        return;
    if (Region->IsLeafNode())
    {
        ImGui::DockNodeSetAddButton(Region->ID, true);
        Requested = ImGui::DockNodeConsumeAddRequest(Region->ID) || Requested;
    }
    else
    {
        ConfigureLeafButtons(Region->ChildNodes[0], Requested);
        ConfigureLeafButtons(Region->ChildNodes[1], Requested);
    }
}
} // namespace

void PanelDockspace::ConstructInterface(float Scale)
{
    Scale       = std::isfinite(Scale) ? std::clamp(Scale, .75f, 1.5f) : 1;
    LayoutScale = Scale;
    auto &Style = ImGui::GetStyle();
    // 📝 Geometry is inherited from pinned EditorHost::ApplyTheme, not a second tab painter or docking algorithm.
    Style.TabSlant        = 14 * Scale;
    Style.TabOverlap      = 24 * Scale;
    Style.TabHeight       = TabHeight * Scale;
    Style.TabStripPadTop  = 4 * Scale;
    Style.TabMinWidthBase = Style.TabMinWidthShrink = TabWidth * Scale;
    Style.DockingNodeHasCloseButton                 = false;
    Style.TabRounding = Style.TabBorderSize = Style.TabBarBorderSize = 0;
    Style.TabButtonRounding                                          = 1;
    const auto *Main                                                 = ImGui::GetMainViewport();
    if (Main->Size.x > 0 && Main->Size.y > 0)
    {
        const ImVec2 Origin(Main->WorkPos.x, Main->WorkPos.y + TopInset * Scale);
        const ImVec2 Extent(Main->WorkSize.x, std::max(1.0f, Main->WorkSize.y - (TopInset + BottomInset) * Scale));
        ImGui::SetNextWindowPos(Origin);
        ImGui::SetNextWindowSize(Extent);
        ImGui::SetNextWindowViewport(Main->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        const auto WindowName = "Frontier panel dockspace###" + DockspaceKey;
        ImGui::Begin(WindowName.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoNavFocus);
        Dockspace = ImGui::GetID(DockspaceKey.c_str());
        if (!Seated)
        {
            ImGui::DockBuilderRemoveNode(Dockspace);
            ImGui::DockBuilderAddNode(Dockspace, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(Dockspace, Origin);
            ImGui::DockBuilderSetNodeSize(Dockspace, Extent);
            if (DefaultColumns.empty())
            {
                for (const auto &Entry : Entries)
                    ImGui::DockBuilderDockWindow(Entry.WindowName.c_str(), Dockspace);
            }
            else
            {
                for (auto &Entry : Entries) { Entry.Placement = 0; Entry.NativeWindow = false; }
                float RemainingWeight = 0;
                for (const auto &Column : DefaultColumns) RemainingWeight += Column.second;
                ImGuiID Remaining = Dockspace;
                for (size_t Index = 0; Index < DefaultColumns.size(); ++Index)
                {
                    const auto &[Key, Weight] = DefaultColumns[Index];
                    ImGuiID Destination = Remaining;
                    if (Index + 1 < DefaultColumns.size())
                        Destination = ImGui::DockBuilderSplitNode(Remaining, ImGuiDir_Left, Weight / RemainingWeight, nullptr, &Remaining);
                    RemainingWeight -= Weight;
                    const auto Entry = std::find_if(Entries.begin(), Entries.end(), [&](const auto &Panel) { return Panel.Specification.Key == Key; });
                    if (Key == StackedLowerKey && !StackedUpperKey.empty())
                    {
                        ImGuiID Lower = 0;
                        const float UpperShare = std::clamp(StackedUpperHeight * Scale / std::max(1.f, Extent.y), .01f, .9f);
                        const ImGuiID Upper = ImGui::DockBuilderSplitNode(Destination, ImGuiDir_Up, UpperShare, nullptr, &Lower);
                        const auto UpperEntry = std::find_if(Entries.begin(), Entries.end(), [&](const auto &Panel)
                            { return Panel.Specification.Key == StackedUpperKey; });
                        ImGui::DockBuilderDockWindow(UpperEntry->WindowName.c_str(), Upper);
                        ImGui::DockBuilderDockWindow(Entry->WindowName.c_str(), Lower);
                    }
                    else ImGui::DockBuilderDockWindow(Entry->WindowName.c_str(), Destination);
                }
            }
            ImGui::DockBuilderFinish(Dockspace);
            if (!PendingLayout.empty())
                RestoreLayout(Scale);
            Seated = true;
        }
        if (Reattach)
        {
            for (auto &Entry : Entries)
                Entry.Placement = -1;
            Reattach = false;
        }
        bool AddRequested = false;
        ConfigureLeafButtons(ImGui::DockBuilderGetNode(Dockspace), AddRequested);
        if (std::none_of(Entries.begin(), Entries.end(), [](const auto &Entry) { return Entry.Specification.Open; }) &&
            ImGui::BeginTabBar("Closed panel catalogue"))
        {
            AddRequested = ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing) || AddRequested;
            ImGui::EndTabBar();
        }
        ImGui::DockSpace(Dockspace, ImVec2(0, 0), ImGuiDockNodeFlags_NoWindowMenuButton);
        if (AddRequested)
            ImGui::OpenPopup("Available panels");
        if (ImGui::BeginPopup("Available panels"))
        {
            for (auto &Entry : Entries)
                if (ImGui::MenuItem(Entry.Specification.Caption.c_str(), nullptr, Entry.Specification.Open,
                                    !Entry.Specification.Available || Entry.Specification.Available()))
                {
                    Entry.Specification.Open = true;
                    Entry.Placement          = -1;
                }
            ImGui::EndPopup();
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
    else if (Dockspace)
        ImGui::DockSpace(Dockspace, ImVec2(0, 0), ImGuiDockNodeFlags_KeepAliveOnly);
    for (auto &Entry : Entries)
    {
        if (!Entry.Specification.Open)
        {
            if (Entry.Presented && Entry.Specification.SuspendContent)
                Entry.Specification.SuspendContent();
            Entry.Presented = false;
            continue;
        }
        if (Entry.RestoreGeometry && Entry.Placement >= 0)
        {
            const bool Native = Entry.NativeWindow && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable);
            const auto Width  = Native ? Entry.Width * Scale : std::min(Entry.Width * Scale, Main->WorkSize.x);
            const auto Height = Native ? Entry.Height * Scale : std::min(Entry.Height * Scale, Main->WorkSize.y);
            const auto X      = Main->Pos.x + Entry.X * Scale;
            const auto Y      = Main->Pos.y + Entry.Y * Scale;
            ImGui::SetNextWindowPos(ImVec2(
                Native ? X : std::clamp(X, Main->WorkPos.x, Main->WorkPos.x + std::max(0.0f, Main->WorkSize.x - Width)),
                Native ? Y
                       : std::clamp(Y, Main->WorkPos.y, Main->WorkPos.y + std::max(0.0f, Main->WorkSize.y - Height))));
            ImGui::SetNextWindowSize(ImVec2(Width, Height));
            Entry.RestoreGeometry = false;
        }
        Entry.RestoreGeometry = false;
        if (Entry.Placement < 0)
        {
            Entry.NativeWindow = false;
            ImGui::DockBuilderDockWindow(Entry.WindowName.c_str(), Dockspace);
            ImGui::SetNextWindowViewport(Main->ID);
        }
        else if (Entry.Placement > 0 && DetachedEnabled &&
                 (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
        {
            Entry.NativeWindow = true;
            ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
            ImGui::SetNextWindowPos(ImVec2(Main->Pos.x + 64, Main->Pos.y + 64), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(640 * Scale, 520 * Scale), ImGuiCond_Always);
        }
        Entry.Placement = 0;
        ImGuiWindowClass WindowClass;
        if (Entry.NativeWindow && DetachedEnabled)
            WindowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&WindowClass);
        if (ImGui::Begin(Entry.WindowName.c_str(), &Entry.Specification.Open))
        {
            if (ImGui::BeginPopupContextWindow("Panel hosting"))
            {
                if (ImGui::MenuItem("Dock back to main window"))
                    Entry.Placement = -1;
                if (ImGui::MenuItem("Detach to OS window", nullptr, false,
                                    DetachedEnabled && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)))
                    Entry.Placement = 1;
                ImGui::EndPopup();
            }
            Entry.Specification.ConstructContent();
            Entry.Presented = true;
        }
        else
        {
            if (Entry.Presented && Entry.Specification.SuspendContent)
                Entry.Specification.SuspendContent();
            Entry.Presented = false;
        }
        ImGui::End();
    }
    RestoreSelection();
}

} // namespace Frontier
