/*==========================================================================================================================================
                                                        PANELDOCKSPACE.H
==========================================================================================================================================*/
// 🧩 Stable panel registrations hosted by the inherited patched ImGui dockspace, without transferring content ownership.

#pragma once

#include "MonitorPlacement.h"
#include <cstdint>
#include <functional>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     PANEL REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

struct DockPanelSpecification
{
    std::string           Key;
    std::string           Caption;
    std::function<void()> ConstructContent;
    bool                  Open = true;
    std::function<void()> SuspendContent;
    std::function<bool()> Available;
};

class PanelDockspace
{
  public:
    void                      RecoverPlatformWindows();
    bool                      Register(DockPanelSpecification Specification);
    [[nodiscard]] toml::table EncodeLayout() const;
    bool                      ConfigureLayout(const toml::table &Layout, std::string &Diagnostic);
    static bool               ValidateLayout(const toml::table &Layout, std::string &Diagnostic) noexcept;
    void                      ResetLayout() noexcept;
    [[nodiscard]] bool        QuerySeated() const noexcept
    {
        return Seated;
    }
    bool ConfigureDockspaceKey(std::string Key);
    void ConfigureWorkspaceInsets(float Top, float Bottom) noexcept;
    void ConfigureTabExtent(float Width, float Height) noexcept;
    bool ConfigureStackedPanels(std::string UpperKey, std::string LowerKey, float UpperHeight);
    bool ConfigureColumns(const std::vector<std::pair<std::string, float>> &Columns);
    bool ConfigureOpen(const std::string &Key, bool Open) noexcept;
    bool ConfigureDetached(const std::string &Key, bool Detached) noexcept;
    void ConfigureDetachedWindows(bool Enabled) noexcept;
    // Call before NewFrame, with both platform/display adapters already initialized.
    bool               ConfigurePlatform() noexcept;
    void               ConstructInterface(float Scale);
    [[nodiscard]] bool QueryOpen(const std::string &Key) const noexcept;
    [[nodiscard]] bool QueryDetachedWindows() const noexcept
    {
        return DetachedEnabled;
    }
    [[nodiscard]] uint32_t QueryDockspace() const noexcept
    {
        return Dockspace;
    }

  private:
    struct DockPanelEntry
    {
        DockPanelSpecification Specification;
        std::string            WindowName;
        int                    Placement       = 0;
        bool                   NativeWindow    = false;
        bool                   Presented       = false;
        bool                   RestoreGeometry = false;
        float                  X = 0, Y = 0, Width = 640, Height = 520;
    };
    toml::table                  CaptureMonitor(WindowRectangle Rectangle) const;
    WindowRectangle              ResolveNativeGeometry(WindowRectangle Rectangle, const toml::table &Section) const;
    std::vector<MonitorWorkArea> MonitorAreas;
    bool                         MonitorRecoveryPending = false;
    void                         RestoreLayout(float Scale);
    void                         RestoreSelection();
    toml::table                  PendingLayout;
    std::vector<std::pair<uint32_t, std::string>> SelectedPanels;
    int                                           SelectionFrames = 0;
    float                                         LayoutScale     = 1;
    std::vector<DockPanelEntry>                   Entries;
    uint32_t                                      Dockspace       = 0;
    bool                                          Seated          = false;
    bool                                          DetachedEnabled = false;
    bool                                          Reattach        = false;
    std::vector<std::pair<std::string, float>> DefaultColumns;
    std::string StackedUpperKey, StackedLowerKey;
    float StackedUpperHeight = 32.0f;
    std::string DockspaceKey = "Frontier shared panels";
    float TopInset = 24, BottomInset = 24;
    float TabWidth = 110, TabHeight = 24;
};

} // namespace Frontier
