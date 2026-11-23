/*==========================================================================================================================================
                                                               NOTCHMOTION.H
==========================================================================================================================================*/
// 🧩 Constrain trapezoidal drawer travel and resolve inherited Android-style pointer release thresholds.

#pragma once

#include "MotionIntegrator.h"
#include "PixelSpace.h"
#include <array>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     NOTCH MOTION
//------------------------------------------------------------------------------------------------------------------------

class NotchMotion
{
  public:
    NotchMotion() noexcept;
    void               ConfigureExtent(float Width, float Height, float Scale) noexcept;
    void               ConfigureOpen(bool Opening) noexcept;
    void               Integrate(double Δτ) noexcept;
    void               IntegratePointer(float CursorX, float CursorY, bool Pressed, bool Down, bool Released, double Δτ,
                                        bool CardContact) noexcept;
    [[nodiscard]] bool QueryOpen() const noexcept { return Opening; }
    [[nodiscard]] bool QueryDragging() const noexcept { return Contact; }
    [[nodiscard]] float                     QueryHeight() const noexcept;
    [[nodiscard]] float                     QueryProgress() const noexcept;
    [[nodiscard]] PlaneExtent               QueryNotchExtent() const noexcept;
    [[nodiscard]] PlaneExtent               QueryGripExtent() const noexcept;
    [[nodiscard]] std::array<PlanePoint, 4> QueryContour() const noexcept;

  private:
    MotionIntegrator Motion;
    uint32_t         Vertical;
    uint32_t         Horizontal;
    float            DisplayWidth      = 1100;
    float            DisplayHeight     = 1000;
    float            Density           = 1;
    float            ContactX          = 0;
    float            ContactY          = 0;
    float            PreviousX         = 0;
    float            PreviousY         = 0;
    double           ContactHeight     = 0;
    double           ContactLateral    = 0;
    double           RateY             = 0;
    double           ContactDuration   = 0;
    bool             Opening           = false;
    bool             Contact           = false;
    bool             NotchContact      = false;
    bool             GripContact       = false;
    bool             AxisResolved      = false;
    bool             VerticalDominant  = false;
    bool             TravelExceeded    = false;
    bool             OpenBeforeContact = false;
};
} // namespace Frontier
