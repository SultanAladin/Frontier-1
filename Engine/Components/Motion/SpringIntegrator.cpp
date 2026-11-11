/*============================================================================================================================================
                                                        SPRINGINTEGRATOR.CPP
============================================================================================================================================*/
// 🧩 Substep-clamped semi-implicit Euler spring-damper integration

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Motion/SpringIntegrator.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  SPRING AXIS OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

void SpringAxis::ConfigurePosition(double Position) noexcept
{
    const double SafePosition = std::isfinite(Position) ? Position : 0.0;
    Current                   = SafePosition;
    Origin                    = SafePosition;
    Target                    = SafePosition;
    Velocity                  = 0.0;
    Settled                   = true;
}

void SpringAxis::Depart(double NewTarget) noexcept
{
    if (!std::isfinite(NewTarget))
    {
        return;
    }
    if (std::fabs(NewTarget - Target) <= SettlePosition && Settled)
    {
        Current  = NewTarget;
        Target   = NewTarget;
        Velocity = 0.0;
        return;
    }
    Origin  = Current;
    Target  = NewTarget;
    Settled = false;
}

void SpringAxis::Integrate(double Δt) noexcept
{
    if (Settled || !std::isfinite(Δt) || Δt <= 0.0)
    {
        return;
    }

    const double ClampedSpan = std::min(Δt, SpringIntegrator::MaximumStepSpan);
    const int    StepCount   = std::max(1, static_cast<int>(std::ceil(ClampedSpan / SpringIntegrator::MaximumSubstep)));
    const double StepΔt      = ClampedSpan / static_cast<double>(StepCount);

    for (int StepIndex = 0; StepIndex < StepCount; ++StepIndex)
    {
        const double Displacement = Current - Target;
        const double Acceleration = -Stiffness * Displacement - Damping * Velocity;
        Velocity += Acceleration * StepΔt;
        Current  += Velocity * StepΔt;
    }

    if (!std::isfinite(Current) || !std::isfinite(Velocity))
    {
        Current  = Target;
        Velocity = 0.0;
        Settled  = true;
        return;
    }

    if (std::fabs(Current - Target) <= SettlePosition && std::fabs(Velocity) <= SettleSpeed)
    {
        Current  = Target;
        Velocity = 0.0;
        Settled  = true;
    }
}

double SpringAxis::Progress() const noexcept
{
    const double Span = Target - Origin;
    if (std::fabs(Span) <= 1e-9)
    {
        return 1.0;
    }
    return std::clamp((Current - Origin) / Span, 0.0, 1.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                SPRING INTEGRATOR METHODS
//------------------------------------------------------------------------------------------------------------------------

uint32_t SpringIntegrator::RegisterAxis(double InitialPosition, double Stiffness, double Damping)
{
    SpringAxis Entry{};
    Entry.ConfigurePosition(InitialPosition);
    Entry.Stiffness = std::max(1.0, Stiffness);
    Entry.Damping   = std::max(0.0, Damping);
    Axes.push_back(Entry);
    return static_cast<uint32_t>(Axes.size() - 1u);
}

void SpringIntegrator::Integrate(double Δt) noexcept
{
    for (SpringAxis& Entry : Axes)
    {
        Entry.Integrate(Δt);
    }
}

bool SpringIntegrator::QueryAllSettled() const noexcept
{
    for (const SpringAxis& Entry : Axes)
    {
        if (!Entry.Settled)
        {
            return false;
        }
    }
    return true;
}

} // namespace Frontier
