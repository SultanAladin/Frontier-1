//============================================================================================================================================
//                                                          MOTIONINTEGRATOR.CPP
//============================================================================================================================================
// 🧩 A semi-implicit spring, a Newton-solved cubic ease, and the one sweep that retires both.

#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     SETTLING CRITERIA
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📐 Both criteria are display-pixel figures and both must hold. A tenth of a pixel is below what any
//    display resolves, and a rate of one pixel per second would take ten seconds to cross that tenth —
//    so a spring meeting both is stationary by every measure the artist has.
constexpr double DisplacementCriterion = 0.1;      // [px]
constexpr double RateCriterion         = 0.001;    // [px/ms] - one pixel per second

// 📐 A tick longer than this is not a slow tick, it is a stalled process — a window drag, a breakpoint, a
//    driver reset. Integrating it whole gives the spring one step of eighty pixels and the drawer arrives
//    somewhere it was never travelling toward. The interval is clamped rather than subdivided because a
//    subdivided stall spends the recovery tick integrating the stall.
constexpr double IntervalLimit = 64.0;   // [ms]

/// 🧩 The abscissa of a cubic Bézier whose endpoints are the origin and unity.
constexpr double CurveX(double Parameter, double FirstControl, double SecondControl)
{
    const double Complement = 1.0 - Parameter;

    return 3.0 * Complement * Complement * Parameter * FirstControl
         + 3.0 * Complement * Parameter  * Parameter * SecondControl
         + Parameter * Parameter * Parameter;
}

/// 🧩 The slope of the same cubic, for the Newton step.
constexpr double CurveSlope(double Parameter, double FirstControl, double SecondControl)
{
    const double Complement = 1.0 - Parameter;

    return 3.0 * Complement * Complement * FirstControl
         + 6.0 * Complement * Parameter  * (SecondControl - FirstControl)
         + 3.0 * Parameter  * Parameter  * (1.0 - SecondControl);
}

// 📐 🔴 The four curves the sources declare, and only those four. The browser's
//    `cubic-bezier(a, b, c, d)` gives the abscissa its controls at a and c and the coordinate its controls at b
//    and d, so each curve below carries four figures and the solve inverts the abscissa before reading it.
struct CurveControl
{
    double  FirstX   = 0.0;
    double  FirstY  = 0.0;
    double  SecondX  = 0.0;
    double  SecondY = 0.0;
};

constexpr CurveControl DeclaredCurves[static_cast<std::uint32_t>(EaseCurve::CurveCount)] =
{
    /* Standard   */ { 0.4,  0.0,  0.2,  1.0 },
    /* Departing  */ { 0.0,  0.0,  0.58, 1.0 },
    /* Carousel   */ { 0.5,  0.05, 0.2,  1.0 },
    /* CssEase    */ { 0.25, 0.1,  0.25, 1.0 }
};

// 📐 Newton from the fraction itself as the initial estimate. The abscissa is monotone and its slope is
//    bounded away from zero over the whole interval for both curves above, so four steps carry the residue
//    below a thousandth — far under a pixel of the traverses these shape.
constexpr std::uint32_t SolveSteps = 4u;

double CurveCoordinate(double Fraction, EaseCurve Declared)
{
    if (Fraction <= 0.0) return 0.0;
    if (Fraction >= 1.0) return 1.0;

    const std::uint32_t Index = static_cast<std::uint32_t>(Declared);
    const CurveControl& Shape   = DeclaredCurves[(Index < static_cast<std::uint32_t>(EaseCurve::CurveCount))
                                               ? Index : 0u];

    double Parameter = Fraction;

    for (std::uint32_t StepIndex = 0u; StepIndex < SolveSteps; ++StepIndex)
    {
        const double Residue = CurveX(Parameter, Shape.FirstX, Shape.SecondX) - Fraction;
        const double Slope   = CurveSlope(Parameter, Shape.FirstX, Shape.SecondX);

        if (std::fabs(Slope) < 1.0e-9)
            break;

        Parameter -= Residue / Slope;

        if (Parameter < 0.0) Parameter = 0.0;
        if (Parameter > 1.0) Parameter = 1.0;
    }

    return CurveX(Parameter, Shape.FirstY, Shape.SecondY);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SPRING
//------------------------------------------------------------------------------------------------------------------------

// 📐 🔴 Semi-implicit Euler: the rate is advanced first and the coordinate is advanced by the **new** rate.
//    The explicit ordering — coordinate first, from the old rate — injects energy at every step and a spring
//    at ζ ≈ 0.935 stops converging entirely somewhere above a 20 ms tick. This ordering is unconditionally
//    stable for every interval the tick ceiling below accepts.
bool SpringInterpolant::Advance(double Elapsed)
{
    if (Settled)
        return false;

    const double Interval = (Elapsed > IntervalLimit) ? IntervalLimit
                          : (Elapsed > 0.0)             ? Elapsed
                                                        : 0.0;

    if (Interval <= 0.0)
        return true;

    // 📐 The coefficients are stated per second, as the source states them, and the interval arrives in
    //    milliseconds. Converting the interval once here is one division; converting the coefficients
    //    instead would convert two figures at every one of the four springs on every tick.
    const double Seconds      = Interval / 1000.0;
    const double Displacement = Current - Target;
    const double Acceleration = -Stiffness * Displacement - Damping * (Rate * 1000.0);

    Rate     = (Rate * 1000.0 + Acceleration * Seconds) / 1000.0;
    Current = Current + Rate * Interval;

    if (std::fabs(Current - Target) < DisplacementCriterion && std::fabs(Rate) < RateCriterion)
    {
        Current = Target;
        Rate     = 0.0;
        Settled  = true;
        return false;
    }

    return true;
}

void SpringInterpolant::Place(double Coordinate)
{
    Current = Coordinate;
    Target   = Coordinate;
    Rate     = 0.0;
    Settled  = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE EASED TRAVERSE
//------------------------------------------------------------------------------------------------------------------------

bool EasedInterpolant::Advance(double Interval)
{
    if (Settled)
        return false;

    const double Accepted = (Interval > IntervalLimit) ? IntervalLimit
                          : (Interval > 0.0)             ? Interval
                                                         : 0.0;

    Elapsed += Accepted;

    if (Elapsed >= Deferral + Duration)
    {
        Elapsed = Deferral + Duration;
        Settled = true;
        return false;
    }

    return true;
}

double EasedInterpolant::Current() const
{
    if (Duration <= 0.0)
        return Incoming;

    if (Elapsed <= Deferral)
        return Previous;

    const double Fraction = (Elapsed - Deferral) / Duration;

    return Previous + (Incoming - Previous) * CurveCoordinate(Fraction, Shape);
}

void EasedInterpolant::Depart(double From, double To, double Over, double Held, EaseCurve Declared)
{
    Previous = From;
    Incoming = To;
    Duration = (Over > 0.0) ? Over : 1.0;
    Deferral = (Held > 0.0) ? Held : 0.0;
    Elapsed  = 0.0;
    Shape    = Declared;
    Settled  = false;
}

void EasedInterpolant::Place(double Coordinate)
{
    Previous = Coordinate;
    Incoming = Coordinate;
    Elapsed  = Deferral + Duration;
    Settled  = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ENROLMENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> MotionIntegrator::RegisterSpring(const MotionScale& Motion, double Applied)
{
    if (SpringCount >= SpringCapacity)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted, "no spring slot remains" });

    SpringInterpolant& Registered = Springs[SpringCount];

    Registered            = {};
    Registered.Stiffness  = Motion.DrawerStiffness;
    Registered.Damping    = Motion.DrawerDamping;
    Registered.Place(Applied);

    return Deliver<std::uint32_t>::Result(SpringCount++);
}

Deliver<std::uint32_t> MotionIntegrator::RegisterEased(double Applied)
{
    if (EaseCount >= EaseCapacity)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted, "no eased slot remains" });

    EasedInterpolant& Registered = Eases[EaseCount];

    Registered = {};
    Registered.Place(Applied);

    return Deliver<std::uint32_t>::Result(EaseCount++);
}

SpringInterpolant& MotionIntegrator::Spring(std::uint32_t Index)
{
    return Springs[(Index < SpringCount) ? Index : 0u];
}

const SpringInterpolant& MotionIntegrator::Spring(std::uint32_t Index) const
{
    return Springs[(Index < SpringCount) ? Index : 0u];
}

EasedInterpolant& MotionIntegrator::Eased(std::uint32_t Index)
{
    return Eases[(Index < EaseCount) ? Index : 0u];
}

const EasedInterpolant& MotionIntegrator::Eased(std::uint32_t Index) const
{
    return Eases[(Index < EaseCount) ? Index : 0u];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE SWEEP
//------------------------------------------------------------------------------------------------------------------------

bool MotionIntegrator::Advance(double Elapsed)
{
    bool Moving = false;

    for (std::uint32_t Index = 0u; Index < SpringCount; ++Index)
        Moving = Springs[Index].Advance(Elapsed) || Moving;

    for (std::uint32_t Index = 0u; Index < EaseCount; ++Index)
        Moving = Eases[Index].Advance(Elapsed) || Moving;

    AnythingMoving = Moving;

    return Moving;
}

bool MotionIntegrator::Moving() const
{
    return AnythingMoving;
}

}   // namespace Slate
