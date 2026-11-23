//============================================================================================================================================
//                                                             CYCLESCHEDULER.H
//============================================================================================================================================
// 🧩 Orders reuse of N cyclic recording slots — the wait that makes a slot writable and the ordinal that names it.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  ONE CYCLE SLOT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one cyclic slot holds — the ordering points a recording against it waits on and signals.
/// note  🔴 The completion is constructed **signalled**. The first recording waits on it before anything has
///       ever been submitted, and an unsignalled one waits for a submission that will never arrive — a
///       bring-up that stops before its first image, with no operand and no error.
/// tag   nonallocating, nonthrowing
struct CycleSlot
{
    VkFence      Completion    = VK_NULL_HANDLE;   // [-] - the host waits on this before writing the slot
    VkSemaphore  ImageAvailable  = VK_NULL_HANDLE;   // [-] - the display signals it; the recording waits on it
    VkSemaphore  RecordingDone = VK_NULL_HANDLE;   // [-] - the recording signals it; the display waits on it
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CYCLE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The cyclic ordering every per-slot resource is sized against and every recording is written into.
/// note  🔴 `06` §7: every per-recording resource is sized against the recording slot count. `RecordingSlotCount`
///       is declared in `Foundation/` because `SlateVulkan` sizes against it and `SlateCompute` quarantines
///       against it — one number, two units, and the count is 🚧 open at `06` §9 between two and three.
/// note  ⚠️ `Advance` is the only writer of the standing ordinal. A caller keeping its own counter and
///       advancing it separately produces two counters that agree for exactly as long as nothing refuses.
/// tag   owning
class CycleScheduler
{
public:

    CycleScheduler()                                 = default;
    CycleScheduler(const CycleScheduler&)            = delete;
    CycleScheduler& operator=(const CycleScheduler&) = delete;
    ~CycleScheduler();

    /// 🧩 Constructs the ordering points for every cycle slot.
    /// in    Exchange  [-]  the created device; borrowed and outlives this component
    /// in    Naming    [-]  names every ordering point; borrowed and outlives this component
    /// out   Result   [-]  refuses with CapabilityAbsent when no device is active, ExtentExhausted when the
    ///                      device declines an ordering point; rejected in full, with nothing half-constructed
    /// post  the standing ordinal is zero and every completion is signalled
    /// note  🔴 `06` §7's diagnostic-name gate. Each of the three points is named by its slot **and** by what it
    ///        orders, because a stall reports one waiter against one signaller and the two are indistinguishable
    ///        by address — an unnamed cycle makes every deadlock report the same sentence.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> ConstructCycleScheduler(const VulkanExchange& Exchange, const DiagnosticExtension& Naming);

    /// 🧩 Waits until the slot the standing ordinal names is no longer read, and makes it writable again.
    /// out   Result  [-]  refuses with HostDenied when the device does not complete within the ceiling, and
    ///                     with DeviceLost when the device was lost; nothing is destroyed either way
    /// post  every resource the slot holds may be amended
    /// note  🔴 The ceiling is finite rather than indefinite. An indefinite wait against a lost device is a
    ///        host that stops with no report, and `06` §7 requires the loss to be reported upward before
    ///        anything is destroyed — which cannot happen from inside a wait that never returns.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Await();

    /// 🧩 Clears the completion of the standing slot, immediately before the submission that signals it.
    /// out   Result  [-]  refuses with HostDenied when the device declines
    /// pre   Await delivered for this slot
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Arm();

    /// 🧩 Carries the standing ordinal to the next slot in the cycle.
    /// post  the ordinal is the previous one raised by one, modulo the slot count
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Advance();

    /// 🧩 The slot the standing ordinal names, for the recording and the display that read it.
    /// out   Result  [-]  refuses with CapabilityAbsent before Construct delivered
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<CycleSlot> Current() const;

    /// 🧩 Which slot in the cycle is standing — what every per-slot claim is addressed by.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t CurrentIndex() const;

    /// 🧩 How many recordings have completed since bring-up, for `86`'s pacing report.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t CompletedRecordings() const;

    /// 🧩 Destroys every ordering point.
    /// pre   the device is idle
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

private:

    // 📝 About two seconds, expressed in the nanoseconds the vendor counts in. Long enough that no honest
    //    rotation reaches it on hardware Slate targets, short enough that a lost device is reported rather
    //    than waited on — the two conditions the ceiling exists between.
    static constexpr std::uint64_t CompletionLimitNanoseconds = 2000000000ull;   // [ns]

    const VulkanExchange*      DeviceEdge   = nullptr;   // [-] - borrowed; never owned
    const DiagnosticExtension* NamingEdge   = nullptr;   // [-] - borrowed; never owned
    std::vector<CycleSlot>  Slots          = {};   // [-] - RecordingSlotCount entries
    std::uint32_t           SlotCurrent   = 0u;   // [-] - which slot is being recorded into
    std::uint64_t           CompletedCount = 0u;   // [-] - recordings completed since bring-up
};

}   // namespace Slate
