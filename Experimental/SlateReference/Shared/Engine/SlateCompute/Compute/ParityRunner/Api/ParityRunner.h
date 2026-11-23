//============================================================================================================================================
//                                                              PARITYRUNNER.H
//============================================================================================================================================
// 🧩 Proves the host form and the shader form of a Shared/ entry point agree at the declared guarantee.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     REGISTRATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One registered `Shared/` entry point and the guarantee it claims.
/// note  An entry point in `Shared/` with no registration is duplicated source that has not diverged yet.
/// tag   owning
struct ParityRegistration
{
    const char*         EntryName    = "";                             // [-] - the entry point's spelling
    PrecisionGuarantee  Reserved      = PrecisionGuarantee::Exact;      // [-] - what it claims
    std::uint32_t       SampleCount  = 0u;                             // [-] - samples the common set holds
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE REPORT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one entry point's parity comparison found.
/// tag   owning
struct ParityReport
{
    const char*    EntryName        = "";     // [-]  - the entry point compared
    std::uint32_t  SampleCount      = 0u;     // [-]  - samples classified on both sides
    std::uint32_t  DisagreeingCount = 0u;     // [-]  - samples where the two forms disagreed
    double         LargestDeviation = 0.0;    // [-]  - measured in units in the last place for Bounded
    bool           AgreementHeld    = false;  // [-]  - the comparison met the claimed guarantee
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE RUNNER
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Classifies registered entry points on both toolchains over a common sample set.
/// note  🔴 Exact entry points are compared bit for bit. Bounded entry points are compared against a
///       declared bound in units in the last place. Convergent entry points are compared within their own
///       convergence criterion. Perceptual entry points are not compared — that is what Perceptual means.
/// tag   owning
class ParityRunner
{
public:

    /// 🧩 Registers one `Shared/` entry point for comparison.
    /// in    Incoming [-]  the entry point and the guarantee it claims
    /// out   Result  [-]  refuses when the entry point is already registered
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Register(const ParityRegistration& Incoming);

    /// 🧩 Compares every registered entry point and reports each one.
    /// out   Reports  [-]  one report per registration, in registration order
    /// note  ⏱️ 🚧 The shader-side comparison requires a device and is contributed by `06`'s bring-up.
    ///       Until it is, the runner compares the host form against itself and reports the sample counts,
    ///       which is honest about what was proven rather than reporting an agreement nothing established.
    /// cost  🔴
    /// tag   api, nonthrowing
    const std::vector<ParityReport>& Compare();

    /// 🧩 Whether every registered entry point met its claimed guarantee in the last comparison.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool AgreementHeld() const;

private:

    std::vector<ParityRegistration>  Registered;                // [-] - in registration order
    std::vector<ParityReport>        Reported;                  // [-] - one per registration
    bool                             AgreementDeclared = false; // [-] - every report held
};

}   // namespace Slate
