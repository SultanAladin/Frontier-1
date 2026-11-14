/*============================================================================================================================================
                                                            PANELEXPANSIONTAB.CPP
============================================================================================================================================*/
// 🧩 Top-edge trapezoidal pull-down notch implementation matching Frontier-OLD 1:1.

#include "Engine/Components/Notch/PanelExpansionTab.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

PanelExpansionTab::PanelExpansionTab() noexcept
    : VerticalAxis(Motion.RegisterAxis(0.0, 225.0, 24.0))
    , HorizontalAxis(Motion.RegisterAxis(0.0, 225.0, 24.0))
{
}

void PanelExpansionTab::ConfigureExtent(float Width, float Height, float Scale) noexcept
{
    DisplayWidth  = std::max(Width, 1.0f);
    DisplayHeight = std::max(Height, 1.0f);
    Density       = std::clamp(Scale, 0.75f, 1.5f);
    if (!Contact && Opening && Motion.Axis(VerticalAxis).Target != static_cast<double>(QueryTravelLimit()))
        Motion.Axis(VerticalAxis).Depart(QueryTravelLimit());

    const double Limit   = std::max(0.0f, (DisplayWidth - QueryTabBounds().Width) * 0.5f);
    SpringAxis  &Lateral = Motion.Axis(HorizontalAxis);
    if (!Contact && std::fabs(Lateral.Target) > Limit)
        Lateral.Depart(std::clamp(Lateral.Target, -Limit, Limit));
}

void PanelExpansionTab::ConfigureTravelLimit(float Height) noexcept
{
    MaximumHeight = std::isfinite(Height) ? std::max(0.0f, Height) : 0.0f;
    if (Opening && !Contact && Motion.Axis(VerticalAxis).Target != static_cast<double>(QueryTravelLimit()))
        Motion.Axis(VerticalAxis).Depart(QueryTravelLimit());
}

float PanelExpansionTab::QueryTravelLimit() const noexcept
{
    return MaximumHeight > 0.0f ? std::min(MaximumHeight, DisplayHeight) : DisplayHeight;
}

float PanelExpansionTab::QueryHeight() const noexcept
{
    return static_cast<float>(Motion.Axis(VerticalAxis).Current);
}

float PanelExpansionTab::QueryProgress() const noexcept
{
    return std::clamp(QueryHeight() / std::max(QueryTravelLimit(), 1.0f), 0.0f, 1.0f);
}

Rectangle2D PanelExpansionTab::QueryTabBounds() const noexcept
{
    const float TabW = std::min(200.0f * Density, DisplayWidth);
    return {(DisplayWidth - TabW) * 0.5f + static_cast<float>(Motion.Axis(HorizontalAxis).Current), QueryHeight(), TabW,
            18.0f * Density};
}

Rectangle2D PanelExpansionTab::QueryGripBounds() const noexcept
{
    return {(DisplayWidth - 48.0f * Density) * 0.5f, QueryHeight() - 30.0f * Density, 48.0f * Density, 6.0f * Density};
}

std::array<Point2D, 4> PanelExpansionTab::QueryTrapezoidContour() const noexcept
{
    const Rectangle2D Bounds = QueryTabBounds();
    const float       Inset  = Bounds.Width * 0.08f;
    return {{{Bounds.X, Bounds.Y},
             {Bounds.Right(), Bounds.Y},
             {Bounds.Right() - Inset, Bounds.Bottom()},
             {Bounds.X + Inset, Bounds.Bottom()}}};
}

void PanelExpansionTab::ConfigureOpen(bool OpenRequested) noexcept
{
    Opening = OpenRequested;
    Contact = false;
    Motion.Axis(VerticalAxis).Depart(Opening ? QueryTravelLimit() : 0.0);
}

void PanelExpansionTab::SnapOpen(bool OpenRequested) noexcept
{
    Opening = OpenRequested;
    Contact = false;
    Motion.Axis(VerticalAxis).ConfigurePosition(Opening ? QueryTravelLimit() : 0.0);
    Motion.Axis(HorizontalAxis).ConfigurePosition(0.0);
}

void PanelExpansionTab::Integrate(double DeltaSeconds) noexcept
{
    (void)Motion.Integrate(DeltaSeconds);
}

void PanelExpansionTab::IntegratePointer(float CursorX, float CursorY, bool Pressed, bool Down, bool Released,
                                         double DeltaSeconds, bool SurfaceContact) noexcept
{
    if (!std::isfinite(CursorX) || !std::isfinite(CursorY) || CursorX < -1.0e8f || CursorY < -1.0e8f)
    {
        if (Contact)
            ConfigureOpen(OpenBeforeContact);
        return;
    }

    const Rectangle2D TabBounds  = QueryTabBounds();
    const float       LocalY     = (CursorY - TabBounds.Y) / std::max(TabBounds.Height, 1.0f);
    const float       Inset      = TabBounds.Width * 0.08f * std::clamp(LocalY, 0.0f, 1.0f);
    const bool        OverTab    = TabBounds.Contains({CursorX, CursorY}) && CursorX >= TabBounds.X + Inset &&
                         CursorX <= TabBounds.Right() - Inset;
    const bool        OverGrip   = QueryGripBounds().Contains({CursorX, CursorY}) && QueryHeight() > 50.0f * Density;

    if (Pressed && (OverTab || OverGrip || (Opening && SurfaceContact)))
    {
        Contact           = true;
        TabContact        = OverTab;
        GripContact       = OverGrip;
        ContactX          = CursorX;
        PreviousX         = CursorX;
        ContactY          = CursorY;
        PreviousY         = CursorY;
        ContactHeight     = Motion.Axis(VerticalAxis).Current;
        ContactLateral    = Motion.Axis(HorizontalAxis).Current;
        ContactDuration   = 0.0;
        RateY             = 0.0;
        AxisResolved      = OverGrip;
        VerticalDominant  = OverGrip;
        TravelExceeded    = false;
        OpenBeforeContact = Opening;
        Motion.Axis(VerticalAxis).ConfigurePosition(ContactHeight);
        Motion.Axis(HorizontalAxis).ConfigurePosition(ContactLateral);
    }

    if (!Contact)
        return;

    ContactDuration += std::max(0.0, DeltaSeconds);
    if (DeltaSeconds > 0.0)
        RateY = 0.60 * RateY + 0.40 * (static_cast<double>(CursorY - PreviousY) / DeltaSeconds);

    const float TravelX = CursorX - ContactX;
    const float TravelY = CursorY - ContactY;
    TravelExceeded      = TravelExceeded || std::fabs(TravelX) > 6.0f * Density || std::fabs(TravelY) > 6.0f * Density;
    if (!AxisResolved && TravelExceeded)
    {
        AxisResolved     = true;
        VerticalDominant = std::fabs(TravelY) > std::fabs(TravelX);
    }

    auto ElasticClamp = [](double Position, double Minimum, double Maximum) noexcept -> double {
        if (Position < Minimum)
            return Minimum - (Minimum - Position) * 0.05;
        if (Position > Maximum)
            return Maximum + (Position - Maximum) * 0.05;
        return Position;
    };

    if (AxisResolved && VerticalDominant)
    {
        Motion.Axis(VerticalAxis).ConfigurePosition(
            ElasticClamp(ContactHeight + static_cast<double>(TravelY), 0.0, QueryTravelLimit()));
    }
    else if (AxisResolved && TabContact)
    {
        const double Limit = std::max(0.0f, (DisplayWidth - TabBounds.Width) * 0.5f);
        Motion.Axis(HorizontalAxis).ConfigurePosition(
            ElasticClamp(ContactLateral + static_cast<double>(TravelX), -Limit, Limit));
    }

    PreviousX = CursorX;
    PreviousY = CursorY;

    if (Released || !Down)
    {
        const bool Tap         = !TravelExceeded && ContactDuration <= 0.350;
        bool       Destination = OpenBeforeContact;
        if (Tap && TabContact)
        {
            Destination = !OpenBeforeContact;
        }
        else if (Tap && GripContact)
        {
            Destination = false;
        }
        else if (AxisResolved && VerticalDominant)
        {
            const double Displacement = static_cast<double>(QueryHeight()) - ContactHeight;
            Destination               = QueryHeight() > QueryTravelLimit() * 0.5f
                              ? !(RateY < -20.0 * Density || Displacement < -50.0 * Density)
                              : (RateY > 20.0 * Density || Displacement > 50.0 * Density);
        }
        Motion.Axis(HorizontalAxis).Depart(0.0);
        ConfigureOpen(Destination);
    }
}

void PanelExpansionTab::Construct(PixelSpace &Surface, const char *Caption, ColorRgba TabFill, ColorRgba LabelAndGripInk,
                             Point2D ViewportOrigin) const noexcept
{
    auto Contour = QueryTrapezoidContour();
    for (Point2D &Pt : Contour)
    {
        Pt.X = (Pt.X + ViewportOrigin.X) / Density;
        Pt.Y = (Pt.Y + ViewportOrigin.Y) / Density;
    }
    Surface.FillPolygon(Contour, TabFill);

    auto ToLogical = [&](Rectangle2D Phys) noexcept -> Rectangle2D {
        return {(Phys.X + ViewportOrigin.X) / Density, (Phys.Y + ViewportOrigin.Y) / Density, Phys.Width / Density,
                Phys.Height / Density};
    };

    Surface.TextCentered(ToLogical(QueryTabBounds()), 12.0f, LabelAndGripInk, Caption);
    if (QueryHeight() > 36.0f * Density)
        Surface.FillRectangle(ToLogical(QueryGripBounds()), LabelAndGripInk, 3.0f);
}

} // namespace Frontier
