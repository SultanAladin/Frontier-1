//============================================================================================================================================
//                                                         INSTRUCTIONEXCHANGE.H
//============================================================================================================================================
// 🧩 The instruction-set specialisation selected once from the host and recorded, so a result names what produced it.

#pragma once

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE SPECIALISATIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which instruction-set specialisation a computation was taken through. Ordered by increasing width.
/// note  🔴 The ordering is load-bearing: a host supporting a wider specialisation supports every narrower one,
///        so a caller selects by comparing rather than by testing each. `02` §7's parity is reported against
///        this ordinal, and a parity failure appearing on exactly one specialisation is otherwise unattributable.
/// tag   nonallocating, nonthrowing
enum class InstructionWidth : std::uint32_t
{
    Baseline = 0u,   // [-] - what the build's own switches guarantee on every host it runs on
    Widened  = 1u,   // [-] - 256-bit floating-point with fused multiply-add
    Extended = 2u    // [-] - 512-bit, and only where the host sustains it rather than merely decodes it
};

/// 🧩 What the host reported about its own instruction set, read once at bring-up.
/// note  📝 Reported separately from the selection because the two differ. `Selected` may be narrower than
///        `Supported` — a host that decodes the widest specialisation but reduces its own clock to sustain it
///        runs the narrower one faster, and `06`'s `HardwareMetrics` needs both figures to explain why.
/// tag   nonallocating, nonthrowing
struct InstructionReport
{
    InstructionWidth  Supported        = InstructionWidth::Baseline;   // [-] - the widest the host decodes
    InstructionWidth  Selected         = InstructionWidth::Baseline;   // [-] - the one computations are taken through
    std::uint32_t     CacheLineBytes   = 0u;                           // [-] - the host's own line extent
    bool              SelectionForced  = false;                        // [-] - a caller fixed it rather than the host
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SELECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The one place an instruction-set specialisation is selected, and the one place the selection is read.
/// note  🔴 Selected **once** and recorded, per `04` §6. A selection re-queried per call site is one that can
///        differ between two call sites in one run, and a parity failure reported against a specialisation that
///        was not the one used names the wrong specialisation — which is worse than naming none.
/// note  ⚠️ Nothing here dispatches. This reports which specialisation was selected; the computation that
///        specialises reads the report and branches once, outside its own inner run. A dispatch surface here
///        would put a function pointer between `Layer1_Numeric` and every arithmetic operation it performs.
/// tag   owning
class InstructionExchange
{
public:

    /// 🧩 Reads the host's instruction set, selects a specialisation, and records both.
    /// out   InstructionReport  [-]  the same report for the rest of the run; Baseline where nothing is reported
    /// note  📝 Called by the first reader rather than by a bring-up sequence, so that a host process which
    ///        never performs a specialised computation never queries for one.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    static const InstructionReport& Report();

    /// 🧩 Fixes the selection at a stated width, overriding what the host reported.
    /// in    Fixed    [-]  never wider than what the host supports; a wider request is reduced to what it does
    /// out   Applied  [-]  the width actually in force after the call
    /// note  🔴 `02` §7's parity runner reads a Tier A predicate through every specialisation the host supports,
    ///        which it cannot do while the selection is fixed at bring-up and never movable. This is what makes
    ///        that sweep possible, and `SelectionForced` is what keeps a forced run from being reported as the
    ///        host's own choice.
    /// note  ⚠️ Never called during a computation. Fixing the width between two halves of one accumulation
    ///        produces a result assembled out of two specialisations, which is the exact defect parity exists
    ///        to detect — reported as a disagreement between the host and itself.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    static InstructionWidth Fix(InstructionWidth Fixed);

    /// 🧩 Returns the selection to the one the host reported.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    static void Release();

    /// 🧩 The selected specialisation as static text, for `86` and for `02` §7's per-registration reporting.
    /// in    Reported  [-]  the width to name
    /// out   Naming    [-]  static text; never allocated
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    static const char* Naming(InstructionWidth Reported);
};

}   // namespace Slate
