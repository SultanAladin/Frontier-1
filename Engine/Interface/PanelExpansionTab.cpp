/*==========================================================================================================================================
                                                              PANELEXPANSIONTAB.CPP
==========================================================================================================================================*/
// 🧩 Adapt the reference drawer's axis lock, elastic carry, flick release, and four-corner notch contour.

#include "PanelExpansionTab.h"
#include "ControlKit.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                      GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

PanelExpansionTab::PanelExpansionTab(PanelExpansionDirectionCategory Orientation) noexcept
    : Direction(Orientation), Vertical(Motion.Register()), Horizontal(Motion.Register())
{
}

void PanelExpansionTab::ConfigureExtent(float Width, float Height, float Scale) noexcept
{
    DisplayWidth  = std::max(Width, 1.0f);
    DisplayHeight = std::max(Height, 1.0f);
    Density       = std::clamp(Scale, 0.75f, 1.5f);
    if (!Contact && Opening && Motion.Spring(Vertical).Target != QueryTravelLimit())
        Motion.Spring(Vertical).Depart(QueryTravelLimit());
    const double Limit   = std::max(0.0f, (DisplayWidth - QueryTabExtent().Width()) * 0.5f);
    auto        &Lateral = Motion.Spring(Horizontal);
    if (!Contact && std::abs(Lateral.Target) > Limit)
        Lateral.Depart(std::clamp(Lateral.Target, -Limit, Limit));
}

float PanelExpansionTab::QueryHeight() const noexcept
{
    return static_cast<float>(Motion.Spring(Vertical).Current);
}
float PanelExpansionTab::QueryProgress() const noexcept
{
    return std::clamp(QueryHeight() / QueryTravelLimit(), 0.0f, 1.0f);
}

PlaneExtent PanelExpansionTab::QueryCanonicalTabExtent() const noexcept
{
    const auto Width = std::min(200.0f * Density, DisplayWidth);
    return Spanning((DisplayWidth - Width) * 0.5f + static_cast<float>(Motion.Spring(Horizontal).Current),
                    QueryHeight(), Width, 18.0f * Density);
}

PlaneExtent PanelExpansionTab::QueryCanonicalGripExtent() const noexcept
{
    return Spanning((DisplayWidth - 48.0f * Density) * 0.5f, QueryHeight() - 30.0f * Density, 48.0f * Density,
                    6.0f * Density);
}

float PanelExpansionTab::QueryTravelLimit() const noexcept
{
    return MaximumHeight > 0 ? std::min(MaximumHeight, DisplayHeight) : DisplayHeight;
}

void PanelExpansionTab::ConfigureTravelLimit(float Height) noexcept
{
    MaximumHeight = std::isfinite(Height) ? std::max(0.0f, Height) : 0;
    if (Opening && !Contact && Motion.Spring(Vertical).Target != QueryTravelLimit())
        Motion.Spring(Vertical).Depart(QueryTravelLimit());
}

PlaneExtent PanelExpansionTab::OrientExtent(PlaneExtent Extent) const noexcept
{
    if (Direction == PanelExpansionDirectionCategory::Upward)
    {
        const auto MinimumY = DisplayHeight - Extent.MaximumY;
        Extent.MaximumY     = DisplayHeight - Extent.MinimumY;
        Extent.MinimumY     = MinimumY;
    }
    return Extent;
}

PlaneExtent PanelExpansionTab::QueryTabExtent() const noexcept
{
    return OrientExtent(QueryCanonicalTabExtent());
}

PlaneExtent PanelExpansionTab::QueryGripExtent() const noexcept
{
    return OrientExtent(QueryCanonicalGripExtent());
}

void PanelExpansionTab::ConstructLayout(PixelSpace &Surface, const char *Caption, PlanePoint Origin) const noexcept
{
    const float Scale   = Density;
    const auto &Palette = ControlKit::Palette();
    auto        Contour = QueryContour();
    for (auto &Point : Contour)
    {
        Point.X = (Point.X + Origin.X) / Scale;
        Point.Y = (Point.Y + Origin.Y) / Scale;
    }
    Surface.FillPolygon(Contour.data(), static_cast<uint32_t>(Contour.size()), Palette.TabBackground);
    const auto TranslateExtent = [&](const PlaneExtent &Extent)
    {
        return PlaneExtent{(Extent.MinimumX + Origin.X) / Scale, (Extent.MinimumY + Origin.Y) / Scale,
                           (Extent.MaximumX + Origin.X) / Scale, (Extent.MaximumY + Origin.Y) / Scale};
    };
    ControlKit::TextCentred(Surface, TranslateExtent(QueryTabExtent()), Palette.TextDim, Caption, 12);
    Surface.FillRectangle(TranslateExtent(QueryGripExtent()), Palette.TextDim);
}

std::array<PlanePoint, 4> PanelExpansionTab::QueryContour() const noexcept
{
    const auto                Bounds  = QueryCanonicalTabExtent();
    const auto                Inset   = Bounds.Width() * 0.08f;
    std::array<PlanePoint, 4> Contour = {{{Bounds.MinimumX, Bounds.MinimumY},
                                          {Bounds.MaximumX, Bounds.MinimumY},
                                          {Bounds.MaximumX - Inset, Bounds.MaximumY},
                                          {Bounds.MinimumX + Inset, Bounds.MaximumY}}};
    if (Direction == PanelExpansionDirectionCategory::Upward)
    {
        for (auto &Point : Contour)
            Point.Y = DisplayHeight - Point.Y;
        std::reverse(Contour.begin(), Contour.end());
    }
    return Contour;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                MOTION AND POINTER CARRY
//------------------------------------------------------------------------------------------------------------------------

void PanelExpansionTab::ConfigureOpen(bool Destination) noexcept
{
    Opening = Destination;
    Contact = false;
    Motion.Spring(Vertical).Depart(Opening ? QueryTravelLimit() : 0);
}

void PanelExpansionTab::Integrate(double Δτ) noexcept
{
    (void)Motion.Integrate(Δτ);
}

void PanelExpansionTab::IntegratePointer(float CursorX, float CursorY, bool Pressed, bool Down, bool Released,
                                         double Δτ, bool CardContact) noexcept
{
    if (!std::isfinite(CursorX) || !std::isfinite(CursorY) || CursorX < -1e8f || CursorY < -1e8f)
    {
        if (Contact)
            ConfigureOpen(OpenBeforeContact);
        return;
    }
    if (Direction == PanelExpansionDirectionCategory::Upward)
        CursorY = DisplayHeight - CursorY;
    const auto Bounds = QueryCanonicalTabExtent();
    const auto LocalY = (CursorY - Bounds.MinimumY) / Bounds.Height();
    const auto Inset  = Bounds.Width() * 0.08f * std::clamp(LocalY, 0.0f, 1.0f);
    const bool OverTab =
        Bounds.Encloses(CursorX, CursorY) && CursorX >= Bounds.MinimumX + Inset && CursorX <= Bounds.MaximumX - Inset;
    const bool OverGrip = QueryCanonicalGripExtent().Encloses(CursorX, CursorY) && QueryHeight() > 50 * Density;
    if (Pressed && (OverTab || OverGrip || (Opening && CardContact)))
    {
        Contact     = true;
        TabContact  = OverTab;
        GripContact = OverGrip;
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
        Motion.Spring(Vertical).ConfigurePosition(Elastic(ContactHeight + TravelY, 0, QueryTravelLimit()));
    else if (AxisResolved && TabContact)
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
        if (Tap && TabContact)
            Destination = !OpenBeforeContact;
        else if (Tap && GripContact)
            Destination = false;
        else if (AxisResolved && VerticalDominant)
        {
            const auto Displacement = QueryHeight() - ContactHeight;
            Destination             = QueryHeight() > QueryTravelLimit() * 0.5f
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
