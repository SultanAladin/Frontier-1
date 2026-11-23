/*==========================================================================================================================================
                                                            MOTIONINTEGRATOR.CPP
==========================================================================================================================================*/
// 🧩 Integrate inherited damped springs at bounded fixed substeps for interface motion.

#include "MotionIntegrator.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{

uint32_t MotionIntegrator::Register(double Initial) noexcept
{
    SpringChannel Channel;
    Channel.ConfigurePosition(Initial);
    Channels.push_back(Channel);
    return static_cast<uint32_t>(Channels.size() - 1u);
}

bool MotionIntegrator::Integrate(double Elapsed) noexcept
{
    if (Elapsed <= 0.0)
        return Moving();

    // Clamp a stalled frame so the shade does not teleport when the window was being dragged.
    Elapsed = std::min(Elapsed, 0.1);

    bool AnyMoving = false;

    for (SpringChannel &Channel : Channels)
    {
        if (Channel.Settled)
            continue;

        double Remaining = Elapsed;
        while (Remaining > 0.0)
        {
            const double Step = std::min(Remaining, Substep);
            Remaining -= Step;

            const double Displacement = Channel.Current - Channel.Target;
            const double Acceleration = -Channel.Stiffness * Displacement - Channel.Damping * Channel.Rate;

            Channel.Rate += Acceleration * Step;
            Channel.Current += Channel.Rate * Step;
        }

        if (std::fabs(Channel.Current - Channel.Target) < PositionTolerance && std::fabs(Channel.Rate) < RateTolerance)
        {
            Channel.Current = Channel.Target;
            Channel.Rate    = 0.0;
            Channel.Settled = true;
        }
        else
        {
            AnyMoving = true;
        }
    }

    return AnyMoving;
}

bool MotionIntegrator::Moving() const noexcept
{
    for (const SpringChannel &Channel : Channels)
        if (!Channel.Settled)
            return true;
    return false;
}

} // namespace Frontier
