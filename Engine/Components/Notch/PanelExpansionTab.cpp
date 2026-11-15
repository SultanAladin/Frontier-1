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
    const double DesiredH = static_cast<double>(QueryDetentHeight(TargetDetent));
    if (!Contact && TargetDetent != DrawerDetentStop::Closed &&
        std::fabs(Motion.Axis(VerticalAxis).Target - DesiredH) > 0.25)
    {
        Motion.Axis(VerticalAxis).Depart(DesiredH);
    }

    const double Limit   = std::max(0.0f, (DisplayWidth - QueryTabBounds().Width) * 0.5f);
    SpringAxis  &Lateral = Motion.Axis(HorizontalAxis);
    if (!Contact && std::fabs(Lateral.Target) > Limit)
        Lateral.Depart(std::clamp(Lateral.Target, -Limit, Limit));
}

void PanelExpansionTab::ConfigureTravelLimit(float Height) noexcept
{
    MaximumHeight = std::isfinite(Height) ? std::max(0.0f, Height) : 0.0f;
    const double DesiredH = static_cast<double>(QueryDetentHeight(TargetDetent));
    if (TargetDetent != DrawerDetentStop::Closed && !Contact &&
        std::fabs(Motion.Axis(VerticalAxis).Target - DesiredH) > 0.25)
    {
        Motion.Axis(VerticalAxis).Depart(DesiredH);
    }
}

float PanelExpansionTab::QueryTravelLimit() const noexcept
{
    return MaximumHeight > 0.0f ? std::min(MaximumHeight, DisplayHeight) : DisplayHeight;
}

float PanelExpansionTab::QueryDetentHeight(DrawerDetentStop Stop) const noexcept
{
    const float Limit = QueryTravelLimit();
    switch (Stop)
    {
        case DrawerDetentStop::Half:
            return std::min(Limit, std::max(Limit * 0.50f, std::min(Limit, 310.0f * Density)));
        case DrawerDetentStop::Full:
            return Limit;
        case DrawerDetentStop::Closed:
        default:
            return 0.0f;
    }
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

void PanelExpansionTab::ConfigureDetent(DrawerDetentStop RequestedDetent) noexcept
{
    TargetDetent = RequestedDetent;
    Contact      = false;
    Motion.Axis(VerticalAxis).Depart(static_cast<double>(QueryDetentHeight(TargetDetent)));
}

void PanelExpansionTab::SnapDetent(DrawerDetentStop RequestedDetent) noexcept
{
    TargetDetent = RequestedDetent;
    Contact      = false;
    Motion.Axis(VerticalAxis).ConfigurePosition(static_cast<double>(QueryDetentHeight(TargetDetent)));
    Motion.Axis(HorizontalAxis).ConfigurePosition(0.0);
}

void PanelExpansionTab::ConfigureOpen(bool OpenRequested) noexcept
{
    ConfigureDetent(OpenRequested ? DrawerDetentStop::Full : DrawerDetentStop::Closed);
}

void PanelExpansionTab::SnapOpen(bool OpenRequested) noexcept
{
    SnapDetent(OpenRequested ? DrawerDetentStop::Full : DrawerDetentStop::Closed);
}

void PanelExpansionTab::Integrate(double DeltaSeconds) noexcept
{
    if (!Contact)
    {
        (void)Motion.Integrate(DeltaSeconds);
    }
}

void PanelExpansionTab::IntegratePointer(float CursorX, float CursorY, bool Pressed, bool Down, bool Released,
                                         double DeltaSeconds, bool SurfaceContact) noexcept
{
    if (!std::isfinite(CursorX) || !std::isfinite(CursorY) || CursorX < -1.0e8f || CursorY < -1.0e8f)
    {
        if (Contact)
        {
            Motion.Axis(HorizontalAxis).Depart(0.0);
            ConfigureDetent(DetentBeforeContact);
        }
        return;
    }

    const Rectangle2D TabBounds  = QueryTabBounds();
    const float       LocalY     = (CursorY - TabBounds.Y) / std::max(TabBounds.Height, 1.0f);
    const float       Inset      = TabBounds.Width * 0.08f * std::clamp(LocalY, 0.0f, 1.0f);
    const bool        OverTab    = TabBounds.Contains({CursorX, CursorY}) && CursorX >= TabBounds.X + Inset &&
                         CursorX <= TabBounds.Right() - Inset;
    const Rectangle2D GripHit    = {
        QueryGripBounds().X - 24.0f * Density,
        QueryGripBounds().Y - 12.0f * Density,
        QueryGripBounds().Width + 48.0f * Density,
        QueryGripBounds().Height + 24.0f * Density,
    };
    const bool        OverGrip   = GripHit.Contains({CursorX, CursorY}) && QueryHeight() > 50.0f * Density;
    const bool        ShadeOpen  = TargetDetent != DrawerDetentStop::Closed || QueryHeight() > 2.0f;

    if (Pressed && (OverTab || OverGrip || (ShadeOpen && SurfaceContact)))
    {
        Contact             = true;
        TabContact          = OverTab;
        GripContact         = OverGrip;
        ContactX            = CursorX;
        PreviousX           = CursorX;
        ContactY            = CursorY;
        PreviousY           = CursorY;
        ContactHeight       = Motion.Axis(VerticalAxis).Current;
        ContactLateral      = Motion.Axis(HorizontalAxis).Current;
        ContactDuration     = 0.0;
        RateY               = 0.0;
        AxisResolved        = OverGrip || (!OverTab && SurfaceContact);
        VerticalDominant    = AxisResolved;
        TravelExceeded      = false;
        DetentBeforeContact = TargetDetent;
        Motion.Axis(VerticalAxis).ConfigurePosition(ContactHeight);
        Motion.Axis(HorizontalAxis).ConfigurePosition(ContactLateral);
    }

    if (!Contact)
        return;

    ContactDuration += std::max(0.0, DeltaSeconds);
    if (DeltaSeconds > 1.0e-5)
    {
        RateY = 0.60 * RateY + 0.40 * (static_cast<double>(CursorY - PreviousY) / DeltaSeconds);
    }

    const float TravelX = CursorX - ContactX;
    const float TravelY = CursorY - ContactY;
    TravelExceeded      = TravelExceeded || std::fabs(TravelX) > 6.0f * Density || std::fabs(TravelY) > 6.0f * Density;
    if (!AxisResolved && TravelExceeded)
    {
        AxisResolved     = true;
        VerticalDominant = std::fabs(TravelY) > std::fabs(TravelX);
    }

    if (AxisResolved && VerticalDominant && TravelExceeded)
    {
        Motion.Axis(VerticalAxis).ConfigurePosition(
            EvaluateElasticClamp(ContactHeight + static_cast<double>(TravelY), 0.0, static_cast<double>(QueryTravelLimit())));
    }
    else if (AxisResolved && TabContact && TravelExceeded)
    {
        const double Limit = std::max(0.0f, (DisplayWidth - TabBounds.Width) * 0.5f);
        Motion.Axis(HorizontalAxis).ConfigurePosition(
            EvaluateElasticClamp(ContactLateral + static_cast<double>(TravelX), -Limit, Limit));
    }

    PreviousX = CursorX;
    PreviousY = CursorY;

    if (Released || !Down)
    {
        const bool       Tap         = !TravelExceeded && ContactDuration <= 0.350;
        DrawerDetentStop Destination = DetentBeforeContact;
        if (Tap && TabContact)
        {
            Destination = (DetentBeforeContact == DrawerDetentStop::Closed)
                              ? DrawerDetentStop::Full
                              : DrawerDetentStop::Closed;
        }
        else if (Tap && GripContact)
        {
            Destination = DrawerDetentStop::Closed;
        }
        else if (AxisResolved && VerticalDominant && TravelExceeded)
        {
            const float CurrentProgress = std::clamp(
                QueryHeight() / std::max(QueryTravelLimit(), 1.0f),
                0.0f,
                1.0f);
            Destination = ResolveDrawerDetentOnRelease(CurrentProgress, RateY, Density, DetentBeforeContact);
        }
        Motion.Axis(HorizontalAxis).Depart(0.0);
        ConfigureDetent(Destination);
    }
}

void PanelExpansionTab::Construct(PixelSpace &Surface, const char *Caption, ColorRgba TabFill, ColorRgba LabelGripInk,
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

    Surface.TextCentered(ToLogical(QueryTabBounds()), 12.0f, LabelGripInk, Caption);
    if (QueryHeight() > 36.0f * Density)
        Surface.FillRectangle(ToLogical(QueryGripBounds()), LabelGripInk, 3.0f);
}

} // namespace Frontier
