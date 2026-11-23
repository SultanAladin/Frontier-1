//============================================================================================================================================
//                                                          PLATFORMINTERCHANGE.H
//============================================================================================================================================
// 🧩 Process, thread and locale services translated once, so that nothing above Layer0 carries an OS conditional.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <string>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE HOST REPORT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the host reports about itself, read once at bring-up and held for the run.
/// note  🔴 Read once and recorded, not queried where it is wanted. `06`'s `HardwareMetrics` attributes a
///       measurement to the host that produced it, and a report re-queried mid-run would attribute two
///       measurements of one run to two different hosts when the operating system revised an answer.
/// tag   nonallocating, nonthrowing
struct HostReport
{
    std::uint32_t  ResolvingCount    = 0u;   // [-] - hardware threads the host reports; never zero once resolved
    std::uint32_t  PhysicalCount     = 0u;   // [-] - physical cores; equals ResolvingCount where none are shared
    std::uint64_t  PhysicalBytes     = 0u;   // [B] - installed physical extent
    std::uint32_t  ExtentGranule     = 0u;   // [B] - the granule a reservation is rounded up to
    std::uint32_t  DisplayScaleMille = 0u;   // [-] - display scale in thousandths; 1000 is unscaled
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The one place process, thread and locale services cross in.
/// note  🔴 Every routine here is a translation and not a policy. `34` decides how many workers to construct;
///       this reports how many the host has. A component that decided the count here would be a scheduling
///       decision living in the layer that is supposed to have no opinions at all.
/// tag   owning
class PlatformInterchange
{
public:

    /// 🧩 Reads the host report, once, and holds it for the run.
    /// out   Result  [-]  refuses with HostDenied when the host declines to describe itself
    /// post  Report returns the same reading for the rest of the run
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Resolve();

    /// 🧩 The host report as read at bring-up.
    /// out   HostReport  [-]  every reading is zero until Resolve has delivered
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const HostReport& Report() const;

    /// 🧩 Names the calling thread, so a host profiler attributes a sample to the work rather than to an ordinal.
    /// in    ThreadName  [-]  static text; never allocated, never retained beyond the call
    /// note  ⚠️ Advisory. A host that declines to record the name is not a failure — `34`'s workers run
    ///        identically either way — so this reports nothing and refuses nothing.
    /// cost  ✔️
    /// tag   api, nonthrowing
    static void DeclareThreadName(const char* ThreadName);

    /// 🧩 The directory the running executable sits in, with a trailing separator.
    /// out   Result  [-]  refuses with HostDenied when the host declines to report the path
    /// note  📝 `06`'s `ShaderCodec` finds its lowered streams beside the executable. Deriving that from the
    ///        working directory instead makes the engine start only when launched from one place, which is
    ///        the defect every debugger's default working directory reproduces on the first run.
    /// cost  🚩
    /// tag   api, nonthrowing
    static Deliver<std::string> ExecutableDirectory();

    /// 🧩 The directory the host sets aside for this application's own retained content.
    /// in    ApplicationName  [-]  the leaf directory; created when it does not yet exist
    /// out   Result          [-]  refuses with HostDenied when the host declines
    /// note  🔴 Retained content never goes beside the executable. On two of the three operating systems the
    ///        executable's own directory is not writable by the artist running it, and the failure appears
    ///        first on the machine that installed the application properly.
    /// cost  🚩
    /// tag   api, nonthrowing
    static Deliver<std::string> RetainedDirectory(const char* ApplicationName);

private:

    HostReport  ResolvedReport   = {};      // [-] - read once by Resolve and never re-queried
    bool        ReportDelivered  = false;   // [-] - Resolve has completed
};

}   // namespace Slate
