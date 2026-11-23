/*==========================================================================================================================================
                                                            MONITORPLACEMENT.H
==========================================================================================================================================*/
// 🧩 Fit floating rectangles to current monitor work areas and reconcile saved display density.

#pragma once
#include <optional>
#include <span>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MONITOR GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

struct WindowRectangle
{
    float X = 0, Y = 0, Width = 640, Height = 520;
};

struct MonitorWorkArea
{
    WindowRectangle Bounds;
    float           Dpi = 1;
};

[[nodiscard]] bool            ValidateMonitor(const MonitorWorkArea &Monitor) noexcept;
[[nodiscard]] WindowRectangle ResolveMonitorPlacement(WindowRectangle                  Rectangle,
                                                      std::optional<MonitorWorkArea>   SavedMonitor,
                                                      std::span<const MonitorWorkArea> AvailableMonitors) noexcept;
} // namespace Frontier
