/*==========================================================================================================================================
                                                               PANELEXPANSIONTAB.H
==========================================================================================================================================*/
// 🧩 Constrain trapezoidal drawer travel and resolve inherited Android-style pointer release thresholds.

#pragma once

#include "MotionIntegrator.h"
#include "PixelSpace.h"
#include <array>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     PANEL EXPANSION TAB
//------------------------------------------------------------------------------------------------------------------------

enum class PanelExpansionDirectionCategory
{
    Downward,
    Upward
};

class PanelExpansionTab
{
  public:
    explicit PanelExpansionTab(
        PanelExpansionDirectionCategory Direction = PanelExpansionDirectionCategory::Downward) noexcept;
    void                ConfigureTravelLimit(float Height) noexcept;
    void                ConstructLayout(PixelSpace &Surface, const char *Caption, PlanePoint Origin) const noexcept;
    void                ConfigureExtent(float Width, float Height, float Scale) noexcept;
    void                ConfigureOpen(bool Opening) noexcept;
    void                Integrate(double Δτ) noexcept;
    void                IntegratePointer(float  CursorX,
                                         float  CursorY,
                                         bool   Pressed,
                                         bool   Down,
                                         bool   Released,
                                         double Δτ,
                                         bool   CardContact) noexcept;
    [[nodiscard]] bool  QueryOpen() const noexcept { return Opening; }
    [[nodiscard]] bool  QueryDragging() const noexcept { return Contact; }
    [[nodiscard]] float QueryHeight() const noexcept;
    [[nodiscard]] float QueryProgress() const noexcept;
    [[nodiscard]] PlaneExtent               QueryTabExtent() const noexcept;
    [[nodiscard]] PlaneExtent               QueryGripExtent() const noexcept;
    [[nodiscard]] std::array<PlanePoint, 4> QueryContour() const noexcept;

  private:
    [[nodiscard]] float       QueryTravelLimit() const noexcept;
    [[nodiscard]] PlaneExtent QueryCanonicalTabExtent() const noexcept;
    [[nodiscard]] PlaneExtent QueryCanonicalGripExtent() const noexcept;
    [[nodiscard]] PlaneExtent OrientExtent(PlaneExtent Extent) const noexcept;

    PanelExpansionDirectionCategory Direction;
    float                           MaximumHeight = 0;
    MotionIntegrator                Motion;
    uint32_t                        Vertical;
    uint32_t                        Horizontal;
    float                           DisplayWidth      = 1100;
    float                           DisplayHeight     = 1000;
    float                           Density           = 1;
    float                           ContactX          = 0;
    float                           ContactY          = 0;
    float                           PreviousX         = 0;
    float                           PreviousY         = 0;
    double                          ContactHeight     = 0;
    double                          ContactLateral    = 0;
    double                          RateY             = 0;
    double                          ContactDuration   = 0;
    bool                            Opening           = false;
    bool                            Contact           = false;
    bool                            TabContact        = false;
    bool                            GripContact       = false;
    bool                            AxisResolved      = false;
    bool                            VerticalDominant  = false;
    bool                            TravelExceeded    = false;
    bool                            OpenBeforeContact = false;
};
} // namespace Frontier
