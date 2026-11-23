/*==========================================================================================================================================
                                                  TYREPROJECTIONDEVICE.CPP
==========================================================================================================================================*/
// 🧩 Validate and pack the standalone tyre projection command without changing vehicle dynamics.

#include "TyreProjectionDevice.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Frontier
{
TyreProjectionCommand CompileTyreProjectionCommand(
    const TyreContinuumInput &Input,
    double                    Seconds,
    std::uint32_t             Substeps)
{
    const auto Finite = [](double Value) { return std::isfinite(Value); };
    for (const double Value : Input.Centre)
        if (!Finite(Value) || std::abs(Value) > 1000000.0)
            throw std::invalid_argument("Tyre projection centre is outside the finite device domain");
    double QuaternionLengthSquared = 0.0;
    for (const double Value : Input.Orientation)
    {
        if (!Finite(Value)) throw std::invalid_argument("Tyre projection orientation must be finite");
        QuaternionLengthSquared += Value * Value;
    }
    double NormalLengthSquared = 0.0;
    for (const double Value : Input.GroundNormal)
    {
        if (!Finite(Value)) throw std::invalid_argument("Tyre projection ground normal must be finite");
        NormalLengthSquared += Value * Value;
    }
    if (std::abs(QuaternionLengthSquared - 1.0) > 0.001 ||
        std::abs(NormalLengthSquared - 1.0) > 0.001)
        throw std::invalid_argument("Tyre projection orientation and ground normal must be normalized");
    if (!Finite(Input.Deflection) || !Finite(Input.Pressure) || !Finite(Input.LateralForce) || !Finite(Seconds) ||
        Input.Deflection > 1.0 || Input.Pressure > 2000000.0 || std::abs(Input.LateralForce) > 10000000.0 ||
        Seconds < 0.000001 || Seconds > 1.0 || Substeps == 0 || Substeps > 32)
        throw std::invalid_argument("Tyre projection dynamics are outside the finite device domain");

    TyreProjectionCommand Command;
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Command.Centre[Axis] = static_cast<float>(Input.Centre[Axis]);
        Command.GroundNormalDeflection[Axis] = static_cast<float>(Input.GroundNormal[Axis]);
    }
    for (int Axis = 0; Axis < 4; ++Axis)
        Command.Orientation[Axis] = static_cast<float>(Input.Orientation[Axis]);
    Command.GroundNormalDeflection[3] = static_cast<float>(std::max(0.0, Input.Deflection));
    Command.PressureLateralSeconds = {static_cast<float>(std::max(0.0, Input.Pressure)),
                                     static_cast<float>(Input.LateralForce), static_cast<float>(Seconds), 0.0f};
    Command.IterationCounts = {Substeps, 0, 0, 0};
    return Command;
}
} // namespace Frontier
