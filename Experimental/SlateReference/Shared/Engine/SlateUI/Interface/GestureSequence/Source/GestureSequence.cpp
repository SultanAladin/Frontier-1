//============================================================================================================================================
//                                                           GESTURESEQUENCE.CPP
//============================================================================================================================================
// 🧩 The contact phase machine — one edge per tick, one accumulation, one smoothed rate.

#include "SlateUI/Interface/GestureSequence/Api/GestureSequence.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      DECLARATION
//------------------------------------------------------------------------------------------------------------------------

void GestureSequence::Declare(const GestureTolerance& Declared)
{
    Tolerance = Declared;
}

const ContactTravel& GestureSequence::Current() const
{
    return Reported;
}

void GestureSequence::Abandon()
{
    if (ContactLive || ReleaseDeferred)
        CancelDeferred = true;

    ContactLive     = false;
    ReleaseDeferred = false;
    Reported        = {};
}

void GestureSequence::Reset()
{
    Tolerance       = {};
    Reported        = {};
    ContactLive     = false;
    ReleaseDeferred = false;
    CancelDeferred  = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE TICK
//------------------------------------------------------------------------------------------------------------------------

const ContactTravel& GestureSequence::Advance(const PointerCondition& Sampled, double Elapsed)
{
    Reported.Phase         = ContactPhase::Absent;
    Reported.TapResolved   = false;
    Reported.FlingResolved = false;

    // ① A cancelled contact is swallowed here rather than reported as a release, so that a consumer never
    //    arbitrates a pose from a drag the display extent invalidated underneath it.
    if (CancelDeferred)
    {
        CancelDeferred = false;

        if (Sampled.ContactHeld)
            return Reported;
    }

    // ② A press and a release that land in the same tick are separated across two ticks. Collapsing them
    //    into one report loses whichever edge the consumer does not look at first.
    if (ReleaseDeferred)
    {
        ReleaseDeferred      = false;
        ContactLive          = false;
        Reported.Phase       = ContactPhase::Released;
        Reported.TapResolved = !Reported.TravelExceeded
                             && Reported.HeldDuration <= Tolerance.TapDurationLimit;
        return Reported;
    }

    // ③ Arrival — everything accumulated is discarded and the origin is stamped.
    if (Sampled.ContactPressed && !ContactLive)
    {
        Reported                = {};
        Reported.Phase          = ContactPhase::Sampled;
        Reported.OriginX    = Sampled.PositionX;
        Reported.OriginY   = Sampled.PositionY;
        Reported.PositionX  = Sampled.PositionX;
        Reported.PositionY = Sampled.PositionY;

        ContactLive     = true;
        ReleaseDeferred = Sampled.ContactReleased;

        return Reported;
    }

    if (!ContactLive)
        return Reported;

    // ④ Carriage — accumulate travel, integrate the smoothed rate, latch the travel ceiling.
    Reported.PositionX  = Sampled.PositionX;
    Reported.PositionY = Sampled.PositionY;
    Reported.TravelX   += static_cast<double>(Sampled.TravelX);
    Reported.TravelY  += static_cast<double>(Sampled.TravelY);
    Reported.HeldDuration  += (Elapsed > 0.0) ? Elapsed : 0.0;

    if (Elapsed > 0.0)
    {
        const double InstantX  = static_cast<double>(Sampled.TravelX)  * 1000.0 / Elapsed;
        const double InstantY = static_cast<double>(Sampled.TravelY) * 1000.0 / Elapsed;

        Reported.RateX  = Reported.RateX  * Tolerance.RateRetention
                            + InstantX  * (1.0 - Tolerance.RateRetention);
        Reported.RateY = Reported.RateY * Tolerance.RateRetention
                            + InstantY * (1.0 - Tolerance.RateRetention);
    }

    // 📐 The ceiling is measured on the displacement from the origin, not on the path length. A contact that
    //    wanders three pixels out and three back is a tap; one that travels ten and holds is not.
    const double Reach = std::sqrt(Reported.TravelX  * Reported.TravelX
                                 + Reported.TravelY * Reported.TravelY);

    if (Reach > static_cast<double>(Tolerance.TapTravelLimit))
        Reported.TravelExceeded = true;

    if (Sampled.ContactReleased || !Sampled.ContactHeld)
    {
        ContactLive            = false;
        Reported.Phase         = ContactPhase::Released;
        Reported.TapResolved   = !Reported.TravelExceeded
                               && Reported.HeldDuration <= Tolerance.TapDurationLimit;
        Reported.FlingResolved = std::fabs(Reported.RateY) > Tolerance.FlingRateFloor;
        return Reported;
    }

    Reported.Phase = ContactPhase::Travelling;
    return Reported;
}

}   // namespace Slate
