//============================================================================================================================================
//                                                          BOOLEANSOLVER.H
//============================================================================================================================================
// 🧩 Exact solid-boolean groundwork. The operation declaration is separate from any current mesh/document path so
//    later boolean implementation can stand on the exact kernel rather than on display geometry. The operand run is
//    variadic from the start: Subtract reads operand zero as the kept solid and every later operand as a cutter,
//    while Unite and Intersect fold across the whole set.

#pragma once

#include <cstdint>
#include <vector>

namespace Slate
{

enum class BooleanSubject : std::uint32_t
{
    Unite = 0u,
    Subtract = 1u,
    Intersect = 2u
};

enum class BooleanDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidSpecification = 1u,
    ImplementationAbsent = 2u,
    Produced = 3u
};

struct BooleanOperand
{
    std::uint32_t TraversedSolid = 0u;
    bool Reversed = false;

    bool Declared() const { return TraversedSolid != 0u; }
};

struct BooleanSpecification
{
    BooleanSubject Subject = BooleanSubject::Unite;
    std::vector<BooleanOperand> OperandSet = {};
    bool PreserveLoops = true;

    bool Declared() const;
};

BooleanDisposition EvaluateBoolean(const BooleanSpecification& Declared);

} // namespace Slate
