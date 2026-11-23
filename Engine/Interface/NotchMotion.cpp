/*==========================================================================================================================================
                                                              NOTCHMOTION.CPP
==========================================================================================================================================*/
// 🧩 Adapt the reference drawer's axis lock, elastic carry, flick release, and four-corner notch contour.

#include "NotchMotion.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                      GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

NotchMotion::NotchMotion() noexcept : Vertical(Motion.Register()), Horizontal(Motion.Register())
{
}

void NotchMotion::ConfigureExtent(float Width, float Height, float Scale) noexcept
{
    DisplayWidth  = std::max(Width, 1.0f);
    DisplayHeight = std::max(Height, 1.0f);
    Density       = std::clamp(Scale, 0.75f, 1.5f);
    if (!Contact && Opening && Motion.Spring(Vertical).Target != DisplayHeight)
        Motion.Spring(Vertical).Depart(DisplayHeight);
    const double Limit   = std::max(0.0f, (DisplayWidth - QueryNotchExtent().Width()) * 0.5f);
    auto        &Lateral = Motion.Spring(Horizontal);
    if (!Contact && std::abs(Lateral.Target) > Limit)
        Lateral.Depart(std::clamp(Lateral.Target, -Limit, Limit));
}

float NotchMotion::QueryHeight() const noexcept
{
    return static_cast<float>(Motion.Spring(Vertical).Current);
}
float NotchMotion::QueryProgress() const noexcept
{
    return std::clamp(QueryHeight() / DisplayHeight, 0.0f, 1.0f);
}

PlaneExtent NotchMotion::QueryNotchExtent() const noexcept
{
    const auto Width = std::min(400.0f * Density, DisplayWidth);
    return Spanning((DisplayWidth - Width) * 0.5f + static_cast<float>(Motion.Spring(Horizontal).Current),
                    QueryHeight(), Width, 36.0f * Density);
}

PlaneExtent NotchMotion::QueryGripExtent() const noexcept
{
    return Spanning((DisplayWidth - 48.0f * Density) * 0.5f, QueryHeight() - 30.0f * Density, 48.0f * Density,
                    6.0f * Density);
}

std::array<PlanePoint, 4> NotchMotion::QueryContour() const noexcept
{
    const auto Bounds = QueryNotchExtent();
    const auto Inset  = Bounds.Width() * 0.08f;
    return {{{Bounds.MinimumX, Bounds.MinimumY},
             {Bounds.MaximumX, Bounds.MinimumY},
             {Bounds.MaximumX - Inset, Bounds.MaximumY},
             {Bounds.MinimumX + Inset, Bounds.MaximumY}}};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                MOTION AND POINTER CARRY
//------------------------------------------------------------------------------------------------------------------------

void NotchMotion::ConfigureOpen(bool Destination) noexcept
{
    Opening = Destination;
    Contact = false;
    Motion.Spring(Vertical).Depart(Opening ? DisplayHeight : 0);
}

void NotchMotion::Integrate(double Δτ) noexcept
{
    (void)Motion.Integrate(Δτ);
}

void NotchMotion::IntegratePointer(float CursorX, float CursorY, bool Pressed, bool Down, bool Released, double Δτ,
                                   bool CardContact) noexcept
{
    if (!std::isfinite(CursorX) || !std::isfinite(CursorY) || CursorX < -1e8f || CursorY < -1e8f)
    {
        if (Contact)
            ConfigureOpen(OpenBeforeContact);
        return;
    }
    const auto Bounds = QueryNotchExtent();
    const auto LocalY = (CursorY - Bounds.MinimumY) / Bounds.Height();
    const auto Inset  = Bounds.Width() * 0.08f * std::clamp(LocalY, 0.0f, 1.0f);
    const bool OverNotch =
        Bounds.Encloses(CursorX, CursorY) && CursorX >= Bounds.MinimumX + Inset && CursorX <= Bounds.MaximumX - Inset;
    const bool OverGrip = QueryGripExtent().Encloses(CursorX, CursorY) && QueryHeight() > 50 * Density;
    if (Pressed && (OverNotch || OverGrip || (Opening && CardContact)))
    {
        Contact      = true;
        NotchContact = OverNotch;
        GripContact  = OverGrip;
        ContactX = PreviousX = CursorX;
        ContactY = PreviousY = CursorY;
        ContactHeight        = Motion.Spring(Vertical).Current;
        ContactLateral       = Motion.Spring(Horizontal).Current;
        ContactDuration = RateY = 0;
        AxisResolved = VerticalDominant = OverGrip;
        TravelExceeded                  = false;
        OpenBeforeContact               = Opening;
        Motion.Spring(Vertical).ConfigurePosition(ContactHeight);
        Motion.Spring(Horizontal).ConfigurePosition(ContactLateral);
    }
    if (!Contact)
        return;
    ContactDuration += std::max(0.0, Δτ);
    if (Δτ > 0)
        RateY = 0.60 * RateY + 0.40 * (CursorY - PreviousY) / Δτ;
    const auto TravelX = CursorX - ContactX;
    const auto TravelY = CursorY - ContactY;
    TravelExceeded     = TravelExceeded || std::abs(TravelX) > 6 * Density || std::abs(TravelY) > 6 * Density;
    if (!AxisResolved && TravelExceeded)
    {
        AxisResolved     = true;
        VerticalDominant = std::abs(TravelY) > std::abs(TravelX);
    }
    const auto Elastic = [](double Position, double Minimum, double Maximum)
    {
        if (Position < Minimum)
            return Minimum - (Minimum - Position) * 0.05;
        if (Position > Maximum)
            return Maximum + (Position - Maximum) * 0.05;
        return Position;
    };
    if (AxisResolved && VerticalDominant)
        Motion.Spring(Vertical).ConfigurePosition(Elastic(ContactHeight + TravelY, 0, DisplayHeight));
    else if (AxisResolved && NotchContact)
    {
        const double Limit = std::max(0.0f, (DisplayWidth - Bounds.Width()) * 0.5f);
        Motion.Spring(Horizontal).ConfigurePosition(Elastic(ContactLateral + TravelX, -Limit, Limit));
    }
    PreviousX = CursorX;
    PreviousY = CursorY;
    if (Released || !Down)
    {
        const bool Tap         = !TravelExceeded && ContactDuration <= 0.350;
        bool       Destination = OpenBeforeContact;
        if (Tap && NotchContact)
            Destination = !OpenBeforeContact;
        else if (Tap && GripContact)
            Destination = false;
        else if (AxisResolved && VerticalDominant)
        {
            const auto Displacement = QueryHeight() - ContactHeight;
            Destination             = QueryHeight() > DisplayHeight * 0.5f
                                          ? !(RateY < -20 * Density || Displacement < -50 * Density)
                                          : (RateY > 20 * Density || Displacement > 50 * Density);
        }
        const double Limit   = std::max(0.0f, (DisplayWidth - Bounds.Width()) * 0.5f);
        auto        &Lateral = Motion.Spring(Horizontal);
        Lateral.Depart(std::clamp(Lateral.Current, -Limit, Limit));
        ConfigureOpen(Destination);
        if (AxisResolved && VerticalDominant)
            Motion.Spring(Vertical).Rate = RateY;
    }
}
} // namespace Frontier
