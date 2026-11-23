/*==========================================================================================================================================
                                                             MOTIONINTEGRATOR.H
==========================================================================================================================================*/
// 🧩 Integrate inherited damped springs at bounded fixed substeps for interface motion.
// Each channel stores one animated displacement and signed velocity.
//
// Each channel is a unit-mass spring:  ẍ = −k (x − target) − c ẋ   integrated semi-implicitly at a fixed
//    substep so a long frame (window drag, breakpoint) never blows the spring up.
//    Defaults k = 225, c = 24 give ζ = c / (2√k) = 0.8 and a 0.6 s settle — the framer-motion solution of
//    Notch's `{ type: "spring", bounce: 0.2, duration: 0.6 }` (findSpring: ζ = 1 − bounce, ω from the
//    0.001 envelope at t = duration → ω ≈ 15 rad/s).

#pragma once

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   SPRING CHANNEL
//------------------------------------------------------------------------------------------------------------------------

struct SpringChannel
{
    double Current   = 0.0;   // [px]    where the quantity is now
    double Target    = 0.0;   // [px]    where it is heading
    double Rate      = 0.0;   // [px/s]  signed velocity
    double Stiffness = 225.0; // [1/s²]  k
    double Damping   = 24.0;  // [1/s]   c
    bool   Settled   = true;  // [-]     |x − target| and |ẋ| under tolerance

    // Pin the value without motion — used while the pointer carries the quantity directly.
    void ConfigurePosition(double Position) noexcept
    {
        Current = Position;
        Target  = Position;
        Rate    = 0.0;
        Settled = true;
    }

    // Ask the spring to travel to Value from wherever it is, keeping any rate it already has.
    void Depart(double Position) noexcept
    {
        Target  = Position;
        Settled = false;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   MOTION INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

class MotionIntegrator
{
  public:
    MotionIntegrator() noexcept = default;

    // Returns the channel ordinal; channels are never released — hosts register a fixed set at construction.
    [[nodiscard]] uint32_t Register(double Initial = 0.0) noexcept;

    [[nodiscard]] SpringChannel       &Spring(uint32_t Ordinal) noexcept { return Channels[Ordinal]; }
    [[nodiscard]] const SpringChannel &Spring(uint32_t Ordinal) const noexcept { return Channels[Ordinal]; }

    // Advances every channel by Elapsed seconds. Returns true while any channel is still moving.
    bool Integrate(double Elapsed) noexcept;

    [[nodiscard]] bool Moving() const noexcept;

  private:
    static constexpr double Substep           = 1.0 / 240.0; // [s]  fixed integration step
    static constexpr double PositionTolerance = 0.05;        // [px]
    static constexpr double RateTolerance     = 1.0;         // [px/s]

    std::vector<SpringChannel> Channels;
};

} // namespace Frontier
