/*============================================================================================================================================
                                                         SPRINGINTEGRATOR.H
============================================================================================================================================*/
// 🧩 Substep-clamped spring-damper integrator for UI drawers, sheets, and smooth transitions

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct SpringAxis
{
    double Current         = 0.0;   // [px]   - Instantaneous integrated coordinate
    double Velocity        = 0.0;   // [px/s] - Instantaneous first derivative
    double Origin          = 0.0;   // [px]   - Departure coordinate of active transition
    double Target          = 0.0;   // [px]   - Equilibrium coordinate
    double Stiffness       = 225.0; // [1/s²] - Hooke spring constant
    double Damping         = 24.0;  // [1/s]  - Linear viscous damping coefficient
    double SettlePosition  = 0.001; // [px]   - Snap tolerance around Target
    double SettleSpeed     = 0.01;  // [px/s] - Velocity tolerance for rest snap
    bool   Settled         = true;  // [-]    - True once snapped onto Target

    void                 ConfigurePosition(double Position) noexcept;
    void                 Depart(double NewTarget) noexcept;
    void                 Integrate(double Δt) noexcept;
    [[nodiscard]] double Progress() const noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class SpringIntegrator
{
public:
    static constexpr double MaximumStepSpan = 0.05;           // [s] - Upper clamp on step Δt before substepping
    static constexpr double MaximumSubstep   = 1.0 / 240.0;   // [s] - Semi-implicit Euler substep ceiling

    SpringIntegrator() noexcept = default;

    uint32_t                  RegisterAxis(double InitialPosition, double Stiffness = 225.0, double Damping = 24.0);
    [[nodiscard]] SpringAxis& Axis(uint32_t Index) noexcept;
    [[nodiscard]] const SpringAxis& Axis(uint32_t Index) const noexcept;
    void                      Integrate(double Δt) noexcept;
    [[nodiscard]] bool        QueryAllSettled() const noexcept;

private:
    std::vector<SpringAxis> Axes {}; // [-] - Registered spring-damper degrees of freedom
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline SpringAxis& SpringIntegrator::Axis(uint32_t Index) noexcept
{
    return Axes[Index];
}

inline const SpringAxis& SpringIntegrator::Axis(uint32_t Index) const noexcept
{
    return Axes[Index];
}

} // namespace Frontier
