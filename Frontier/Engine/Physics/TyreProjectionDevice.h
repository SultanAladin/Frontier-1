/*==========================================================================================================================================
                                                   TYREPROJECTIONDEVICE.H
==========================================================================================================================================*/
// 🧩 Float32 command and particle footprints for independent GPU tyre-carcass projection.
#pragma once

#include "TyreContinuum.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Frontier
{

struct alignas(16) TyreProjectionCommand
{
    std::array<float, 4> Centre{};
    std::array<float, 4> Orientation{0.0f, 0.0f, 0.0f, 1.0f};
    std::array<float, 4> GroundNormalDeflection{0.0f, 0.0f, 1.0f, 0.0f};
    std::array<float, 4> PressureLateralSeconds{};
    std::array<std::uint32_t, 4> IterationCounts{};
};

struct alignas(16) TyreProjectionParticle
{
    std::array<float, 4> RestInverseMass{};
    std::array<float, 4> Local{};
    std::array<float, 4> Previous{};
    std::array<float, 4> Velocity{};
    std::array<float, 4> World{};
};

// 📝 ForceStatus[0] is the demo's prescribed force in newtons; ForceStatus[1] is 1 for a valid command,
//    0 when rejected by the shader. The force is not a solved tyre/road reaction.
struct alignas(16) TyreProjectionResult
{
    std::array<float, 4> ForceStatus{};
};

static_assert(sizeof(float) == 4 && sizeof(std::uint32_t) == 4);
static_assert(sizeof(TyreProjectionCommand) == 80 && alignof(TyreProjectionCommand) == 16);
static_assert(offsetof(TyreProjectionCommand, Orientation) == 16);
static_assert(offsetof(TyreProjectionCommand, GroundNormalDeflection) == 32);
static_assert(offsetof(TyreProjectionCommand, PressureLateralSeconds) == 48);
static_assert(offsetof(TyreProjectionCommand, IterationCounts) == 64);
static_assert(sizeof(TyreProjectionParticle) == 80 && alignof(TyreProjectionParticle) == 16);
static_assert(offsetof(TyreProjectionParticle, Local) == 16 && offsetof(TyreProjectionParticle, Previous) == 32);
static_assert(offsetof(TyreProjectionParticle, Velocity) == 48 && offsetof(TyreProjectionParticle, World) == 64);
static_assert(sizeof(TyreProjectionResult) == 16 && alignof(TyreProjectionResult) == 16);

// 📝 Inputs are clamped as in the demo for negative deflection and pressure, then checked against
//    the explicitly bounded float32 device domain. The caller owns buffers, queue synchronization and submission.
[[nodiscard]] TyreProjectionCommand CompileTyreProjectionCommand(
    const TyreContinuumInput &Input,
    double                    Seconds,
    std::uint32_t             Substeps);

} // namespace Frontier
