//============================================================================================================================================
//                                                            HOSTLIFECYCLE.H
//============================================================================================================================================
// 🧩 The five nested lifetimes every host owns — constructed in one order, recovered once, unwound in reverse.

#pragma once

// 📝 `DeviceOffering` is an alias for the shared `DeviceAttachment`; see the note there.
#include "Shared/DeviceAttachment.slang.h"

#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"
#include "SlateMath/Platform/WindowInterchange/Api/WindowInterchange.h"
#include "SlateVulkan/Device/CommandSequence/Api/CommandSequence.h"
#include "SlateVulkan/Device/CycleScheduler/Api/CycleScheduler.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/DisplayScheduler/Api/DisplayScheduler.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <cstdint>

namespace Slate
{

// 📝 ⚠️ `Lifecycle` is not one of the nineteen closed role suffixes in `SKILL-Naming.md`. The spelling is
//    declared here deliberately rather than by oversight: it is the name the project asked for, and the
//    mechanism it states — a span with a construction and a matching destruction — is the one thing the
//    closed list has no entry for. Recorded so that a reader finds a decision here rather than a drift.

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE FIVE LIFETIMES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How long one owned resource stands before something must reclaim it.
/// note  🔴 The ordinals are the construction order, and the reverse is the reclamation order. A device is
///        reclaimed only after every display resource inside it, and a display only after every recording
///        that may still be reading one of its images. Reversing that is a use-after-free the validation
///        layer reports as a destroyed-object access, several frames after the call that caused it.
/// note  A recovery reclaims a suffix of this enumeration and never a subset from the middle. Device loss
///        retires Device and everything after it; a resize retires Display and everything after it.
/// tag   guarantee
enum class ResourceLifetime : std::uint32_t
{
    Host          = 0u,   // [-] - the window, the instance, the timeline; retired only at exit
    Device        = 1u,   // [-] - the logical device and the diagnostic attachment
    Display       = 2u,   // [-] - the presentation chain and its images
    Recording     = 3u,   // [-] - the cyclic slots and the command recordings
    InterfaceTick = 4u,   // [-] - one tick of interface content; retired every tick
    LifetimeCount = 5u    // [-] - the closed count, never a lifetime
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT A HOST DECLARES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Everything a host states once, before anything is constructed.
/// tag   guarantee, nonallocating, nonthrowing
struct HostDeclaration
{
    const char*    Naming        = "Host";     // [-]  - static text; names this host in every report
    const char*    WindowCaption = "Slate";    // [-]  - static text
    std::uint32_t  InitialWidth  = 1280u;      // [px]
    std::uint32_t  InitialHeight =  720u;      // [px]
    bool           DiagnosticRequested = false;// [-]  - the vendor's validation layers
    LatencyIntent  Pacing        = LatencyIntent::SteadyPacing;   // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT THE DEVICE OFFERS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every device handle a layer above this one needs in order to attach to what was constructed.
/// note  🔴 Declared here rather than reached for from `SlateUI`. `InterfaceAttachment` is the same set of
///        handles, but it lives one layer up, and a component in `SlateVulkan` that included it would
///        invert the dependency partition — the compiler rejected exactly that, which is the partition
///        working. A host copies the eight members across; the copy is the seam.
/// note  ⚠️ The image counts are restated on every display recovery, so a reader that took a copy before a
///        resize holds two figures the chain no longer has.
/// tag   guarantee, nonallocating, nonthrowing

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT ONE TICK CARRIES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the vendor's diagnostic layers reported across the whole run, counted by verdict.
/// note  🔴 `86` §5 is the authority on which of the seven dispositions is a problem, and this states the
///        counts rather than a judgement. `Serious` is the one figure that carries a judgement, and it
///        counts `Rejected` and `Failed` alone — the two rows §5's table marks as problems outright.
/// note  ⚠️ `Terminated` is excluded from `Serious` deliberately. §5 marks it **sometimes** a problem and
///        requires it to be presented as ambiguous rather than resolved; folding it into a pass-or-fail
///        figure would resolve it here, one layer below where §5 declares that decision.
/// note  ⚠️ `Received` and `Retained` differ by two mechanisms, and the difference is load-bearing.
///        `ReportSequence` coalesces a recurrence into one entry with a count, and it discards the oldest
///        at `RetainedLimit`. A reader that sees `Retained` alone cannot tell a clean run from one whose
///        first error was discarded four thousand arrivals ago, which is why `Discarded` is stated.
/// note  📝 A run in a configuration that negotiated no diagnostic reports `Negotiated == false` and zeroes
///        throughout. That is not a clean run and a reader must be able to tell the two apart.
/// tag   guarantee, nonallocating, nonthrowing
struct DiagnosticVerdict
{
    bool           Negotiated = false;   // [-] - the sink attached; without it every figure below is meaningless
    std::uint64_t  Received    = 0u;      // [-] - raw arrivals at the sink, coalescing and discards included
    std::uint32_t  Retained   = 0u;      // [-] - entries standing in the register now
    std::uint64_t  Appended   = 0u;      // [-] - occurrences appended across the session
    std::uint64_t  Discarded  = 0u;      // [-] - retained entries the ceiling dropped
    std::uint32_t  Serious    = 0u;      // [-] - retained entries `86` §5 marks as a problem
};

/// 🧩 What `Await` decided about this tick before any content was built.
/// note  🔴 A host reads `Current` and nothing else to decide whether to record. `Idle` is not an
///        error — it is the ordinary answer on a minimised window, a resized chain, or a tick the vendor
///        rejected — and a host that treats it as one exits on the first resize.
/// tag   guarantee
enum class TickCondition : std::uint32_t
{
    Recording     = 0u,   // [-] - an image is acquired and a recording is open; the host must record
    Idle     = 1u,   // [-] - nothing was acquired; the host records nothing and asks again
    Closed        = 2u,   // [-] - the window was closed, or the device was lost beyond recovery
    ConditionCount = 3u    // [-] - the closed count, never a standing
};

/// 🧩 One tick's arrangement, valid only until `Complete` or the next `Await`.
/// note  ⚠️ `Recording` is a vendor handle and is deliberately the only one that crosses. A host records
///        into it through the interface seam; nothing here hands out the device, the chain or the slots.
/// tag   guarantee, nonallocating, nonthrowing
struct TickPass
{
    TickCondition     Current        = TickCondition::Idle;   // [-]
    VkCommandBuffer  Recording       = VK_NULL_HANDLE;            // [-]  - open, outside a render scope until BeginDisplay
    double           ElapsedMilliseconds = 0.0;                   // [ms] - since the previous tick
    std::uint32_t    Width     = 0u;                        // [px] - the drawable extent this tick
    std::uint32_t    Height    = 0u;                        // [px]
    bool             DisplayAltered  = false;                     // [-]  - the chain was re-established
    bool             DeviceRetiring  = false;                     // [-]  - retire device resources THIS tick
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Constructs the five lifetimes in `32` §1's order, recovers them, and unwinds them in the exact reverse.
/// note  🔴 This component exists because three hosts had written the same bring-up, the same recovery and
///        the same teardown, and the three copies had drifted. the texturing host never tested `ExtentAltered`, so
///        resizing did nothing until the vendor rejected; it left the tick loop on a rejected present rather
///        than re-establishing the chain; and it opened its command recording **before** building the
///        interface tick, so five of its escape paths returned to the top of the loop with a command buffer
///        still recording and a display image still acquired. A second windowed host had none of those, and
///        the only thing separating the two was which copy a reader happened to edit.
/// note  📝 That second host was `EditorHost`, and once this component held the bring-up it was byte-identical
///        to the texturing host but for its name and window caption. `32` §3 ships two hosts — texturing and the
///        headless `ConsoleHost` — and named it in neither, so it was retired rather than left as a second
///        copy of one program waiting to drift again.
/// note  🔴 ⏱️ The ordering that prevents the whole class of defect is stated once, here: **every refusal
///        that can occur is resolved before the display image is acquired**. After `Await` reports
///        `Recording`, there is no path in this component that returns without submitting — so a host
///        cannot leave a recording open, because it never holds one across a decision.
/// note  ⚠️ A host owns its own content and this component owns none of it. What crosses is a command
///        open recording plus an explicit display boundary; what does not cross is the device, the chain, the
///        cyclic slots, or any means of reaching them.
/// tag   owning
class HostLifecycle
{
public:

    // 🔴 A device lost more than twice in one session is a driver that is not coming back. Rebuilding
    //    without a ceiling presents the artist with a window that never draws, which is strictly worse
    //    than one that reports the loss and exits.
    static constexpr std::uint32_t DeviceRecoveryLimit = 2u;   // [-] - rebuilds accepted per session

    HostLifecycle()                                = default;
    HostLifecycle(const HostLifecycle&)            = delete;
    HostLifecycle& operator=(const HostLifecycle&) = delete;
    ~HostLifecycle();

    /// 🧩 Constructs Host, Device, Display and Recording in `32` §1's declared order.
    /// in    Declared  [-]  what the host states once; the naming is retained for every report
    /// out   Result   [-]  refuses at the first stage that declines, naming it, having reclaimed whatever
    ///                      earlier stages had already constructed
    /// note  🔴 A partial construction is never delivered. Every refusal path unwinds what it built, so a
    ///        host that checks the delivery and returns leaks nothing.
    /// post  on delivery, `Interface` carries every handle the interface seam needs
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructHost(const HostDeclaration& Declared);

    /// 🧩 Opens one tick — drains input, recovers the display if it moved, acquires an image, and opens a
    ///    command recording outside any rendering scope.
    /// in    ClearInk  [-]  what the colour target is cleared to, as four unit ordinates
    /// out   Pass      [-]  `Recording` when the host must record, `Idle` when it must not, `Closed`
    ///                      when the loop must end
    /// note  🔴 Every refusal is resolved **before** the image is acquired. That ordering is the whole of
    ///        the resize defect: nothing after the acquire can decline, so nothing after it can return.
    /// note  ⚠️ A `Idle` tick has acquired nothing and opened nothing. Calling `Complete` after one
    ///        is rejected rather than submitting an empty recording.
    /// cost  🚩
    /// tag   api, nonthrowing
    TickPass Await(const float ClearInk[4]);

    /// 🧩 ⏱️ Whether the artist has touched this window since the previous tick — the wake rule's one
    ///    input the interface cannot supply.
    /// out   Stirred [-]  true when pointer, button, wheel, key or focus arrived at the last drain
    /// note  📝 A pass-through to the window, so a host holding only a lifecycle can ask.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Stirred() const;

    /// 🧩 ⏱️ Sleeps in the window system until the artist does something or the interval expires.
    /// in    Seconds [-]  the longest this may sleep
    /// note  🔴 ⏱️ CALLED INSTEAD OF RECORDING A TICK, NEVER AFTER ACQUIRING ONE. `Await` acquires a
    ///        display image and opens a recording; surrendering that through `Complete` without
    ///        recording anything presents the CLEAR GROUND, because the display scope `Complete`
    ///        opens on the host's behalf carries `LOAD_OP_CLEAR`. An idle editor built that way wipes
    ///        itself to the clear ink and stays there with its chrome alive, which reads as a black
    ///        window rather than as a still one. The wake question is therefore asked BEFORE the
    ///        tick is opened, and this is what the host waits in when the answer is no.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Doze(double Seconds);

    /// 🧩 Opens the display's dynamic-rendering scope after scene/offscreen recordings have completed.
    /// out   Result  [-]  refuses when no tick stands or when the display scope already stands
    /// note  🔴 `Await` deliberately leaves the command recording outside any render scope. Classic scene
    ///        constructs, transfer work and compute dispatches must record before this call; interface and
    ///        display overlays record after it. Vulkan forbids nesting a classic render pass inside dynamic
    ///        rendering, so opening the display in `Await` made the declared geometry schedule impossible.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> BeginDisplay();

    /// 🧩 Closes the rendering scope, submits the recording, presents, and advances the cycle.
    /// out   Result  [-]  refuses when no tick stands recording; a rejected present is recovered here and
    ///                     is not reported as a refusal
    /// note  🔴 A rejected present re-establishes the chain rather than ending the loop. It is the ordinary
    ///        answer to a resize that arrived between the acquire and the present.
    /// post  no recording is open; the next Await may proceed
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Complete();

    /// 🧩 Whether the host should keep ticking.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Active() const;

    /// 🧩 Every device handle a layer above needs, filled from what was constructed.
    /// note  The image counts are restated on every display recovery, so a host that reads this after a
    ///       resize sees the counts the chain actually holds.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    DeviceOffering Offering() const;

    /// 🧩 The diagnostic edge, for components the host constructs beside the device.
    /// note  🔴 Read-only. The extension is constructed with the instance and lives for the device; a
    ///        component borrowing it must outlive no more than one device rebuild.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const DiagnosticExtension& DiagnosticsExtension() const;

    /// 🧩 The device exchange, for components the host constructs beside the device.
    /// note  🔴 Read-only. The exchange is re-constructed on a device rebuild, so a component borrowing
    ///        it must be reconstructed in the same branch.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const VulkanExchange& DeviceExchange() const;

    /// 🧩 The window, for a host that needs the native handle or the closure condition.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    WindowInterchange&       Window();
    const WindowInterchange& Window() const;

    /// 🧩 The timeline every duration in this process is measured against.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const TickSequence& Timeline() const;

    /// 🧩 Whether a display recovery happened since the host last asked, and clears the record of it.
    /// use   A host calls this to renegotiate its own display-sized content exactly once per recovery.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool DisplayRecovered();

    /// 🧩 Asks for the device tier to be rebuilt at the start of the next tick.
    /// note  🔴 Two-phase by necessity. The tick this is asked on returns `Idle` with `DeviceRetiring`
    ///        raised, so the host retires its device resources WHILE THE DEVICE STILL STANDS; the rebuild
    ///        happens next tick. Rebuilding at once destroyed the device before the host had been told,
    ///        and the host's own reclamation then idled a dead handle.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void AskDeviceRebuild();

    /// 🧩 Retires the device tier and rebuilds it, counting against nothing.
    /// note  🔴 Distinct from `RecoverDevice` deliberately. `DeviceRecoveryLimit` bounds LOSSES, because a
    ///        device the driver keeps losing is one that is not coming back; a rebuild the artist asked for
    ///        is evidence of nothing and must not spend that budget. Spamming the diagnostic key otherwise
    ///        closed the host on the third press, which reads as the rebuild having crashed it.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> RebuildDevice();

    /// 🧩 Retires the device tier and rebuilds it, leaving the window, instance and surface standing.
    /// out   Result  [-]  refuses when the rebuild declines, having left nothing half-constructed
    /// use   Called on a reported device loss, and by the diagnostic key that exercises this path.
    /// note  🔴 🚧 Every device resource a HOST owns is invalid once this returns — its pipelines, its
    ///        descriptor sets, the interface's font atlas. `DeviceRecovered` reports that, and a host that
    ///        does not read it records into handles the vendor has returned.
    /// note  ⚠️ The Host tier is deliberately untouched. `Construct` opens a window at step ②, so calling it
    ///        again to recover would stand a second window in front of the artist.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> RecoverDevice();

    /// 🧩 Whether the device tier was rebuilt since the host last asked, and clears the record of it.
    /// use   A host calls this to rebuild every device resource it owns, exactly once per recovery.
    /// note  🔴 Distinct from `DisplayRecovered`. A display recovery invalidates what was sized to the
    ///        extent; a device recovery invalidates everything, the display included.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool DeviceRecovered();

    /// 🧩 What the vendor's diagnostic layers reported across the run so far, counted by verdict.
    /// out   Verdict  [-]  zeroed with `Negotiated == false` where no sink attached
    /// use   A host states this once at teardown, so a run under the validation layers ends in a figure
    ///       rather than in a console a reader has to scroll back through.
    /// note  ⚠️ Read before `Reclaim`. The register is Device lifetime, and a reclaimed device has emptied it.
    /// cost  🚩
    /// tag   api, nonthrowing
    DiagnosticVerdict Diagnostics() const;

    /// 🧩 States the diagnostic verdict on the console, in the one format every host reports with.
    /// out   Serious  [-]  how many retained entries `86` §5 marks as a problem; zero is a clean run
    /// use   A host returns this from `main` so a validation run has an exit code and not only text.
    /// note  🔴 Every serious entry is named individually before the summary. A count alone says a run was
    ///        dirty without saying what it tripped, which is one rebuild short of useless.
    /// note  ⚠️ Read before `Reclaim`, for the reason `Diagnostics` states.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::uint32_t StateDiagnostics() const;

    /// 🧩 Reclaims every lifetime at or beyond the declared one, in reverse construction order.
    /// in    From     [-]  the earliest lifetime to retire; everything after it is retired first
    /// note  🔴 Waits for the device to go idle before retiring anything device-side. A chain reclaimed
    ///        while a recording still reads one of its images is a use-after-free the validation layer
    ///        reports several frames later, against a call that has already returned.
    /// cost  🔴
    /// tag   api, nonthrowing
    void Reclaim(ResourceLifetime From = ResourceLifetime::Host);

private:

    Deliver<bool> EstablishDisplay(std::uint32_t Width, std::uint32_t Height);
    bool          RecoverDisplay();

    /// 🧩 Retires an acquired image whose `ImageAvailable` no submission is going to wait down.
    /// note  🔴 Every path that returns between the acquire and the submission calls this. A binary
    ///        semaphore is unsignalled only by a wait, so one left signalled is signalled a second time by
    ///        the next acquire on that slot — and the chain it is pending against cannot be destroyed
    ///        until the acquire is retired.
    void          SettleAcquisition();

    HostDeclaration      Declared          = {};               // [-] - as stated, never re-read
    TickSequence         Clock             = {};               // [-] - Host lifetime
    WindowInterchange    Surface           = {};               // [-] - Host lifetime
    VulkanExchange       DeviceEdge        = {};               // [-] - Host, then Device
    ReportSequence       DiagnosticRegister= {};               // [-] - Device lifetime
    DiagnosticExtension  DiagnosticEdge    = {};               // [-] - Device lifetime
    DisplayScheduler     DisplayChain      = {};               // [-] - Display lifetime
    CycleScheduler       Cycle             = {};               // [-] - Recording lifetime
    CommandSequence      Commands          = {};               // [-] - Recording lifetime

    VkSurfaceKHR         PresentationSurface = VK_NULL_HANDLE; // [-] - Host lifetime, after the instance
    TickPoint            PreviousTick      = {};               // [-]
    AcquiredImage         TickImage         = {};               // [-] - valid only while a tick records
    std::uint32_t        SlotIndex       = 0u;               // [-] - the cycle slot this tick took
    VkCommandBuffer      OpenRecording     = VK_NULL_HANDLE;   // [-] - non-null only between Await and Complete
    float                DisplayClear[4]   = {};               // [-] - retained from Await until BeginDisplay
    bool                 TickRecording     = false;            // [-] - a tick stands at TickCondition::Recording
    bool                 DisplayScopeOpen  = false;            // [-] - dynamic display rendering stands
    bool                 DisplayAltered    = false;            // [-] - a recovery the host has not adopted
    bool                 DeviceAltered     = false;            // [-] - a device rebuild the host has not adopted
    bool                 DeviceRebuildAsked= false;            // [-] - asked for; serviced next tick
    bool                 DeviceRetiring    = false;            // [-] - phase one done; the rebuild is next
    bool                 ResizeStorming    = false;            // [-] - re-establish every tick until released
    std::uint32_t        DeviceRecoveries  = 0u;               // [-] - rebuilds attempted; the retry is bounded
    bool                 LoopActive      = false;            // [-] - false once the window closed
    ResourceLifetime     Constructed       = ResourceLifetime::Host;   // [-] - how far bring-up reached
};

}   // namespace Slate
