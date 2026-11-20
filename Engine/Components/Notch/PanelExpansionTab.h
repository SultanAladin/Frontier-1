/*============================================================================================================================================
                                                             PANELEXPANSIONTAB.H
============================================================================================================================================*/
// 🧩 Top-edge trapezoidal pull-down notch ("Frontier") with elastic carry, axis lock, and flick physics

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Appearance/ThemeRepository.h"
#include "Engine/Components/Motion/DrawerDetentGesture.h"
#include "Engine/Components/Motion/SpringIntegrator.h"
#include "Engine/Components/PixelSpace.h"

#include <array>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class PanelExpansionTab
{
public:
    PanelExpansionTab() noexcept;

    void ConfigureExtent(float Width, float Height, float Scale) noexcept;
    void ConfigureTravelLimit(float Height) noexcept;
    void ConfigureDetent(DrawerDetentStop RequestedDetent) noexcept;
    void SnapDetent(DrawerDetentStop RequestedDetent) noexcept;
    void ConfigureOpen(bool OpenRequested) noexcept;
    void SnapOpen(bool OpenRequested) noexcept;

    void Integrate(double DeltaSeconds) noexcept;
    void IntegratePointer(
        float  CursorX,
        float  CursorY,
        bool   Pressed,
        bool   Down,
        bool   Released,
        double DeltaSeconds,
        bool   SurfaceContact = false) noexcept;

    [[nodiscard]] bool                   QueryOpen() const noexcept;
    [[nodiscard]] DrawerDetentStop       QueryDetent() const noexcept;
    [[nodiscard]] bool                   QueryActiveContact() const noexcept;
    [[nodiscard]] bool                   QueryVerticalDragging() const noexcept;
    [[nodiscard]] float                  QueryHeight() const noexcept;
    [[nodiscard]] float                  QueryDetentHeight(DrawerDetentStop Stop) const noexcept;
    [[nodiscard]] float                  QueryProgress() const noexcept;
    [[nodiscard]] float                  QueryTravelLimit() const noexcept;
    [[nodiscard]] Rectangle2D            QueryTabBounds() const noexcept;
    [[nodiscard]] Rectangle2D            QueryGripBounds() const noexcept;
    [[nodiscard]] std::array<Point2D, 4> QueryTrapezoidContour() const noexcept;

    void Construct(
        PixelSpace& Surface,
        const char* Caption,
        ColorRgba   TabFill,
        ColorRgba   LabelGripInk,
        Point2D     ViewportOrigin = {}) const noexcept;

private:
    SpringIntegrator Motion              {};       // [-]   - Vertical and lateral spring solver
    uint32_t         VerticalAxis        = 0u;     // [idx] - Shade vertical extension spring index
    uint32_t         HorizontalAxis      = 0u;     // [idx] - Lateral elastic carry spring index

    float            DisplayWidth        = 1280.0f;// [px]  - Host viewport width
    float            DisplayHeight       = 720.0f; // [px]  - Host viewport height
    float            Density             = 1.0f;   // [-]   - DPI density scale factor
    float            MaximumHeight       = 0.0f;   // [px]  - Custom travel ceiling override (0 = full height)

    DrawerDetentStop TargetDetent        = DrawerDetentStop::Closed; // [-] - Target 3-detent stop (Closed/Half/Full)
    DrawerDetentStop DetentBeforeContact = DrawerDetentStop::Closed; // [-] - Detent prior to drag start
    bool             Contact             = false;  // [-]   - Pointer drag session active
    bool             TabContact          = false;  // [-]   - Contact initiated on top trapezoid tab
    bool             GripContact         = false;  // [-]   - Contact initiated on bottom grip bar
    bool             AxisResolved        = false;  // [-]   - Drag dominant axis locked
    bool             VerticalDominant    = false;  // [-]   - Vertical axis won lock threshold
    bool             TravelExceeded      = false;  // [-]   - Tap-vs-drag deadzone crossed
    float            ContactX            = 0.0f;   // [px]  - Initial pointer X coordinate
    float            ContactY            = 0.0f;   // [px]  - Initial pointer Y coordinate
    float            PreviousX           = 0.0f;   // [px]  - Previous step pointer X coordinate
    float            PreviousY           = 0.0f;   // [px]  - Previous step pointer Y coordinate
    double           ContactHeight       = 0.0;    // [px]  - Shade height at drag start
    double           ContactLateral      = 0.0;    // [px]  - Lateral offset at drag start
    double           ContactDuration     = 0.0;    // [s]   - Elapsed drag duration
    double           RateY               = 0.0;    // [px/s]- Smoothed vertical flick velocity
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline bool PanelExpansionTab::QueryOpen() const noexcept
{
    return TargetDetent != DrawerDetentStop::Closed;
}

inline DrawerDetentStop PanelExpansionTab::QueryDetent() const noexcept
{
    return TargetDetent;
}

inline bool PanelExpansionTab::QueryActiveContact() const noexcept
{
    return Contact;
}

inline bool PanelExpansionTab::QueryVerticalDragging() const noexcept
{
    return Contact && AxisResolved && VerticalDominant && TravelExceeded;
}

} // namespace Frontier
