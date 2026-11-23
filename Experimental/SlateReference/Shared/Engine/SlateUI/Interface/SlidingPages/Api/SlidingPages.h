//============================================================================================================================================
//                                                          SLIDINGPAGES.H
//============================================================================================================================================
// 🧩 Deterministic page placement shared by every horizontally travelling interface page pair.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"

namespace Slate
{

/// 🧩 The two extents participating in one page transition.
/// note  Departing and Incoming retain the supplied page size; the caller confines drawing to Viewport.
struct SlidingPagePlacement
{
    PlaneExtent Departing = {};
    PlaneExtent Incoming  = {};
    bool        Travelling = false;
};

/// 🧩 Resolves page geometry without owning panel state, rendering, or interaction.
class SlidingPages
{
public:

    /// 🧩 Reserves one eased travel and seats the strip at its initial page.
    Deliver<bool> ConstructSlidingPages(MotionIntegrator& IncomingMotion, std::uint32_t InitialPage,
                                        double Duration = 260.0,
                                        EaseCurve Shape = EaseCurve::Carousel);

    /// 🧩 Starts travel toward a page when it differs from the current destination.
    void Navigate(std::uint32_t IncomingPage);

    /// 🧩 Places one member of an arbitrary-length page strip for the current travel.
    PlaneExtent Page(const PlaneExtent& Viewport, std::uint32_t PageIndex) const;

    /// 🧩 Whether the shared eased travel has not reached its destination.
    bool Travelling() const;

    std::uint32_t CurrentPage() const { return ArrivingPage; }
    std::uint32_t PreviousPage() const { return DepartingPage; }
    std::uint32_t MotionSlot() const { return TravelMotion; }

    void Reset();

    /// 🧩 Places the departing and incoming pages across one viewport.
    /// in    Progress [-]  zero at the old page, one at the new page; values outside are clamped
    /// in    Forward  [-]  true moves content toward the leading edge, false toward the trailing edge
    /// out   Placement [-] deterministic full-size page extents and whether travel remains in progress
    /// tag   api, guarantee, nonallocating, nonthrowing
    static constexpr SlidingPagePlacement Place(const PlaneExtent& Viewport, float Progress, bool Forward)
    {
        const float Travel = Progress < 0.0f ? 0.0f : (Progress > 1.0f ? 1.0f : Progress);
        const float Bearing = Forward ? 1.0f : -1.0f;
        const float Span = Viewport.Width();
        const float DepartingX = Viewport.MinimumX - Bearing * Span * Travel;
        const float IncomingX = Viewport.MinimumX + Bearing * Span * (1.0f - Travel);

        return SlidingPagePlacement{
            Spanning(DepartingX, Viewport.MinimumY, Span, Viewport.Height()),
            Spanning(IncomingX, Viewport.MinimumY, Span, Viewport.Height()),
            Travel > 0.0f && Travel < 1.0f
        };
    }

private:

    MotionIntegrator* Motion        = nullptr;
    std::uint32_t     TravelMotion  = 0u;
    std::uint32_t     DepartingPage = 0u;
    std::uint32_t     ArrivingPage  = 0u;
    double            TravelDuration = 260.0;
    EaseCurve         TravelShape    = EaseCurve::Carousel;
};

}   // namespace Slate
