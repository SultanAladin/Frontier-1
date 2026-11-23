/*==========================================================================================================================================
                                                           MONITORPLACEMENT.CPP
==========================================================================================================================================*/
// 🧩 Recover finite floating-window geometry without requiring GLFW, ImGui, a renderer or a physical monitor.

#include "MonitorPlacement.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MONITOR CONSTRAINTS
//------------------------------------------------------------------------------------------------------------------------

bool ValidateMonitor(const MonitorWorkArea &Monitor) noexcept
{
    const auto &Bounds = Monitor.Bounds;
    return std::isfinite(Bounds.X) && std::isfinite(Bounds.Y) && std::isfinite(Bounds.Width) &&
           std::isfinite(Bounds.Height) && std::abs(Bounds.X) <= 1000000 && std::abs(Bounds.Y) <= 1000000 &&
           Bounds.Width >= 1 && Bounds.Width <= 32768 && Bounds.Height >= 1 && Bounds.Height <= 32768 &&
           std::isfinite(Monitor.Dpi) && Monitor.Dpi >= .25f && Monitor.Dpi <= 8;
}

WindowRectangle ResolveMonitorPlacement(WindowRectangle Rectangle, std::optional<MonitorWorkArea> SavedMonitor,
                                        std::span<const MonitorWorkArea> AvailableMonitors) noexcept
{
    if (!ValidateMonitor({Rectangle, 1}))
        Rectangle = {};
    if (SavedMonitor && !ValidateMonitor(*SavedMonitor))
        SavedMonitor.reset();
    const auto             Reference       = SavedMonitor ? SavedMonitor->Bounds : Rectangle;
    const MonitorWorkArea *Selected        = nullptr;
    double                 MaximumOverlap  = -1;
    double                 MinimumDistance = std::numeric_limits<double>::max();
    // 📝 Work-area overlap retains a surviving monitor; nearest work area recovers a removed display.
    for (const auto &Monitor : AvailableMonitors)
    {
        if (!ValidateMonitor(Monitor))
            continue;
        const auto  &Bounds     = Monitor.Bounds;
        const double Width      = std::max(0.f, std::min(Reference.X + Reference.Width, Bounds.X + Bounds.Width) -
                                                    std::max(Reference.X, Bounds.X));
        const double Height     = std::max(0.f, std::min(Reference.Y + Reference.Height, Bounds.Y + Bounds.Height) -
                                                    std::max(Reference.Y, Bounds.Y));
        const double Horizontal = Reference.X + Reference.Width / 2 -
                                  std::clamp(Reference.X + Reference.Width / 2, Bounds.X, Bounds.X + Bounds.Width);
        const double Vertical   = Reference.Y + Reference.Height / 2 -
                                  std::clamp(Reference.Y + Reference.Height / 2, Bounds.Y, Bounds.Y + Bounds.Height);
        const double Distance   = Horizontal * Horizontal + Vertical * Vertical;
        if (Width * Height > MaximumOverlap || (Width * Height == MaximumOverlap && Distance < MinimumDistance))
        {
            Selected        = &Monitor;
            MaximumOverlap  = Width * Height;
            MinimumDistance = Distance;
        }
    }
    if (!Selected)
        return Rectangle;
    const auto &Bounds = Selected->Bounds;
    if (SavedMonitor)
    {
        const float DensityRatio = Selected->Dpi / SavedMonitor->Dpi;
        Rectangle.X              = Bounds.X + (Rectangle.X - SavedMonitor->Bounds.X) * DensityRatio;
        Rectangle.Y              = Bounds.Y + (Rectangle.Y - SavedMonitor->Bounds.Y) * DensityRatio;
        Rectangle.Width *= DensityRatio;
        Rectangle.Height *= DensityRatio;
    }
    Rectangle.Width  = std::clamp(Rectangle.Width, 1.f, Bounds.Width);
    Rectangle.Height = std::clamp(Rectangle.Height, 1.f, Bounds.Height);
    Rectangle.X      = std::clamp(Rectangle.X, Bounds.X, Bounds.X + Bounds.Width - Rectangle.Width);
    Rectangle.Y      = std::clamp(Rectangle.Y, Bounds.Y, Bounds.Y + Bounds.Height - Rectangle.Height);
    return Rectangle;
}
} // namespace Frontier
