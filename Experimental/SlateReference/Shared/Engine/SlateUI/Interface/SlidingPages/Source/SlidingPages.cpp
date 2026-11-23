//============================================================================================================================================
//                                                         SLIDINGPAGES.CPP
//============================================================================================================================================
// 🧩 Compile-time geometry proofs for the shared horizontal page placement.

#include "SlateUI/Interface/SlidingPages/Api/SlidingPages.h"

namespace Slate
{

Deliver<bool> SlidingPages::ConstructSlidingPages(MotionIntegrator& IncomingMotion, std::uint32_t InitialPage,
                                                  double Duration, EaseCurve Shape)
{
    if (Motion != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "sliding pages are already constructed" });

    const Deliver<std::uint32_t> Registered = IncomingMotion.RegisterEased(1.0);
    if (!Registered.Resolved)
        return Deliver<bool>::Refuse(Registered.Error);

    Motion         = &IncomingMotion;
    TravelMotion   = Registered.Resolve();
    DepartingPage  = InitialPage;
    ArrivingPage   = InitialPage;
    TravelDuration = Duration;
    TravelShape    = Shape;
    return Deliver<bool>::Result(true);
}

void SlidingPages::Navigate(std::uint32_t IncomingPage)
{
    if (Motion == nullptr || IncomingPage == ArrivingPage)
        return;

    DepartingPage = ArrivingPage;
    ArrivingPage  = IncomingPage;
    Motion->Eased(TravelMotion).Depart(0.0, 1.0, TravelDuration, 0.0, TravelShape);
}

PlaneExtent SlidingPages::Page(const PlaneExtent& Viewport, std::uint32_t PageIndex) const
{
    const float Progress = Motion != nullptr ? static_cast<float>(Motion->Eased(TravelMotion).Current()) : 1.0f;
    const float Departing = static_cast<float>(DepartingPage);
    const float Arriving  = static_cast<float>(ArrivingPage);
    const float StripPosition = Departing + (Arriving - Departing) * Progress;
    return Spanning(Viewport.MinimumX + (static_cast<float>(PageIndex) - StripPosition) * Viewport.Width(),
                    Viewport.MinimumY, Viewport.Width(), Viewport.Height());
}

bool SlidingPages::Travelling() const
{
    return Motion != nullptr && !Motion->Eased(TravelMotion).Settled;
}

void SlidingPages::Reset()
{
    Motion          = nullptr;
    TravelMotion    = 0u;
    DepartingPage   = 0u;
    ArrivingPage    = 0u;
    TravelDuration  = 260.0;
    TravelShape     = EaseCurve::Carousel;
}

namespace
{

constexpr PlaneExtent ProofViewport = Spanning(10.0f, 20.0f, 100.0f, 40.0f);
constexpr SlidingPagePlacement ForwardStart = SlidingPages::Place(ProofViewport, 0.0f, true);
constexpr SlidingPagePlacement ForwardHalf  = SlidingPages::Place(ProofViewport, 0.5f, true);
constexpr SlidingPagePlacement ForwardEnd   = SlidingPages::Place(ProofViewport, 1.0f, true);
constexpr SlidingPagePlacement BackwardHalf = SlidingPages::Place(ProofViewport, 0.5f, false);
constexpr SlidingPagePlacement Clamped      = SlidingPages::Place(ProofViewport, 2.0f, true);

static_assert(ForwardStart.Departing.MinimumX == 10.0f && ForwardStart.Incoming.MinimumX == 110.0f);
static_assert(ForwardHalf.Departing.MinimumX == -40.0f && ForwardHalf.Incoming.MinimumX == 60.0f);
static_assert(ForwardEnd.Departing.MinimumX == -90.0f && ForwardEnd.Incoming.MinimumX == 10.0f);
static_assert(BackwardHalf.Departing.MinimumX == 60.0f && BackwardHalf.Incoming.MinimumX == -40.0f);
static_assert(Clamped.Incoming.MinimumX == 10.0f && !Clamped.Travelling);
static_assert(ForwardHalf.Departing.Width() == ProofViewport.Width());
static_assert(ForwardHalf.Incoming.Height() == ProofViewport.Height());

}   // namespace
}   // namespace Slate
