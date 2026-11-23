/*==========================================================================================================================================
                                                             BARRIER DERIVATION
                                     ------------------------------------------------------------------
                                 Per-target usage walks over the resolved order become transition records.
==========================================================================================================================================*/

// 🧩 Barrier derivation: per-target usage walks over the resolved order become transition records.

#pragma once

#include "RenderTarget.h"
#include "SubmissionOrder.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        STRUCTS
//------------------------------------------------------------------------------------------------------------------------

enum class BarrierKind : uint8_t
{
    LayoutTransition,     // Usage changed: the SDK phase emits an image layout transition
    ExecutionDependency   // Same usage, but a write is involved: execution/memory dependency only
};

struct DerivedBarrier
{
    std::string Target;         // [-]
    uint32_t    BeforePosition; // [position] - Submission position the barrier precedes (intra-unit when a
                                //              unit touches one target with two usages in a row)
    TargetUsage From;           // [-] - Usage before the barrier (Undefined = discard-birth)
    TargetUsage To;             // [-] - Usage after the barrier
    BarrierKind Kind;
};

struct DerivedBarriers
{
    std::vector<DerivedBarrier> Barriers;   // [barriers] - In submission-walk order
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Walks each declared target's touches in resolved submission order from its resting usage
// (transient targets are born Undefined; persistent targets declare where their history rests).
// A usage change emits a LayoutTransition; a repeated usage with a write on either side emits
// an ExecutionDependency; read-after-read in the same usage emits nothing. Returns false for
// undeclared touched targets or resting-usage contract violations (see implementation notes).
[[nodiscard]] bool DeriveBarriers(const std::vector<SubmissionUnitDeclaration>& Units,
                                  const ResolvedSubmissionOrder&                Order,
                                  const std::vector<RenderTargetDeclaration>&   Targets,
                                  DerivedBarriers&                              Out,
                                  std::string&                                  Diagnostic);

// Writes the derivation to the Render log stream (the Phase 3 "barrier dump" evidence):
// one line per barrier, then a per-frame summary counted by kind.
void DumpBarriers(const DerivedBarriers&                        Barriers,
                  const std::vector<SubmissionUnitDeclaration>& Units,
                  const ResolvedSubmissionOrder&                Order);

} // namespace Frontier
