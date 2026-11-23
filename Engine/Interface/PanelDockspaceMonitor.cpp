/*==========================================================================================================================================
                                                        PANELDOCKSPACEMONITOR.CPP
==========================================================================================================================================*/
// 🧩 Archive native monitor geometry and recover secondary GLFW windows after work-area or display-density changes.

#include "PanelDockspace.h"
#include <algorithm>
#include <imgui_internal.h>

namespace Frontier
{
namespace
{
std::vector<MonitorWorkArea> EnumerateWorkAreas()
{
    std::vector<MonitorWorkArea> Areas;
    for (const auto &Monitor : ImGui::GetPlatformIO().Monitors)
    {
        MonitorWorkArea Area{{Monitor.WorkPos.x, Monitor.WorkPos.y, Monitor.WorkSize.x, Monitor.WorkSize.y},
                             Monitor.DpiScale};
        if (ValidateMonitor(Area))
            Areas.push_back(Area);
    }
    return Areas;
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     SAVED MONITOR GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

toml::table PanelDockspace::CaptureMonitor(WindowRectangle Rectangle) const
{
    const auto             Areas          = EnumerateWorkAreas();
    const MonitorWorkArea *Selected       = nullptr;
    float                  MaximumOverlap = -1;
    for (const auto &Area : Areas)
    {
        const auto &Bounds  = Area.Bounds;
        const float Overlap = std::max(0.f, std::min(Rectangle.X + Rectangle.Width, Bounds.X + Bounds.Width) -
                                                std::max(Rectangle.X, Bounds.X)) *
                              std::max(0.f, std::min(Rectangle.Y + Rectangle.Height, Bounds.Y + Bounds.Height) -
                                                std::max(Rectangle.Y, Bounds.Y));
        if (Overlap > MaximumOverlap)
        {
            Selected       = &Area;
            MaximumOverlap = Overlap;
        }
    }
    if (!Selected)
        return {};
    const auto &Bounds = Selected->Bounds;
    return toml::table{{"x", Bounds.X},           {"y", Bounds.Y},        {"width", Bounds.Width},
                       {"height", Bounds.Height}, {"dpi", Selected->Dpi}, {"window_x", Rectangle.X},
                       {"window_y", Rectangle.Y}};
}

WindowRectangle PanelDockspace::ResolveNativeGeometry(WindowRectangle Rectangle, const toml::table &Section) const
{
    auto Areas = EnumerateWorkAreas();
    if (Areas.empty())
    {
        const auto *Main = ImGui::GetMainViewport();
        Areas.push_back({{Main->WorkPos.x, Main->WorkPos.y, Main->WorkSize.x, Main->WorkSize.y}, Main->DpiScale});
    }
    std::optional<MonitorWorkArea> Saved;
    if (const auto *Monitor = Section["monitor"].as_table())
    {
        Saved       = MonitorWorkArea{{Monitor->at("x").value_or(0.f), Monitor->at("y").value_or(0.f),
                                       Monitor->at("width").value_or(1.f), Monitor->at("height").value_or(1.f)},
                                      Monitor->at("dpi").value_or(1.f)};
        Rectangle.X = Monitor->at("window_x").value_or(Rectangle.X);
        Rectangle.Y = Monitor->at("window_y").value_or(Rectangle.Y);
    }
    return ResolveMonitorPlacement(Rectangle, Saved, Areas);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LIVE WORK-AREA RECOVERY
//------------------------------------------------------------------------------------------------------------------------

void PanelDockspace::RecoverPlatformWindows()
{
    const auto Areas   = EnumerateWorkAreas();
    bool       Changed = MonitorRecoveryPending || Areas.size() != MonitorAreas.size();
    if (!Changed)
        for (size_t Index = 0; Index < Areas.size(); ++Index)
        {
            const auto &Current  = Areas[Index];
            const auto &Previous = MonitorAreas[Index];
            Changed |= Current.Bounds.X != Previous.Bounds.X || Current.Bounds.Y != Previous.Bounds.Y ||
                       Current.Bounds.Width != Previous.Bounds.Width ||
                       Current.Bounds.Height != Previous.Bounds.Height || Current.Dpi != Previous.Dpi;
        }
    MonitorAreas   = Areas;
    auto &Platform = ImGui::GetPlatformIO();
    if (!Changed || Areas.empty() || !Platform.Platform_SetWindowPos || !Platform.Platform_SetWindowSize ||
        !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
        return;
    // 📝 Before NewFrame: platform request markers let native ImGui propagate the recovered viewport to its dock groups.
    // 📝 ImGui performs live DPI scaling itself; only restored archives apply a saved/current density ratio here.
    MonitorRecoveryPending = false;
    for (int Index = 1; Index < Platform.Viewports.Size; ++Index)
    {
        auto *Viewport = Platform.Viewports[Index];
        if (!Viewport->PlatformHandle)
            continue;
        if (Viewport->Flags & ImGuiViewportFlags_IsMinimized)
        {
            MonitorRecoveryPending = true;
            continue;
        }
        const auto Position = Platform.Platform_GetWindowPos ? Platform.Platform_GetWindowPos(Viewport) : Viewport->Pos;
        const auto Size = Platform.Platform_GetWindowSize ? Platform.Platform_GetWindowSize(Viewport) : Viewport->Size;
        const auto Recovered = ResolveMonitorPlacement({Position.x, Position.y, Size.x, Size.y}, std::nullopt, Areas);
        if (Recovered.X != Position.x || Recovered.Y != Position.y)
        {
            Platform.Platform_SetWindowPos(Viewport, ImVec2(Recovered.X, Recovered.Y));
            Viewport->PlatformRequestMove = true;
        }
        if (Recovered.Width != Size.x || Recovered.Height != Size.y)
        {
            Platform.Platform_SetWindowSize(Viewport, ImVec2(Recovered.Width, Recovered.Height));
            Viewport->PlatformRequestResize = true;
        }
    }
}
} // namespace Frontier
