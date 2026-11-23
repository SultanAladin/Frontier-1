//============================================================================================================================================
//                                                            HARDWAREMETRICS.H
//============================================================================================================================================
// 🧩 Hardware execution duration, measured between recorded timestamps — and reported unavailable rather than zero.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  ONE MEASURED SPAN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one declared span of device execution reads, for the rotation the reading came back from.
/// note  🔴 `Available` is a member rather than an absent duration. `08` §5: unavailable is not zero. A span on
///        a device with no timestamp capability, and a span whose rotation has not completed yet, both have no
///        duration — and a zero in either place is a performance report that is confidently wrong.
/// note  ⚠️ The duration lags by the recording slot count. A timestamp is read back only once the rotation that
///        recorded it has completed, so the reading a tick presents is `RecordingSlotCount` rotations old.
///        Waiting for the current one would serialise the host against the device to measure it.
/// tag   nonallocating, nonthrowing
struct MeasuredSpan
{
    const char*    Declared      = "";      // [-]  - static text naming the span; the recording's own identity
    double         Duration      = 0.0;     // [ms] - device execution, meaningful only while Available holds
    std::uint64_t  RecordingRead  = 0u;      // [-]  - which completed rotation the reading came from
    bool           Available     = false;   // [-]  - a reading came back; never inferred from a zero duration
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE MEASURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The declared spans of device execution, the timestamps recorded around each, and the depth they nest to.
/// note  🔴 `06` §1: measures hardware execution duration **and depth**. The depth is the nesting of one
///        declared span inside another — `08` §3's ordering is thirteen recordings and a report that timed each
///        in isolation could not say that ⑤·i and ⑤·ii together account for what ⑤ costs.
/// note  🔴 `08` §5's substitution, executed rather than declared: with `TimestampQueryAvailable` false every
///        span reports unavailable and every recording still records. `Open` and `Close` deliver as no-ops so
///        that no recording site branches on the capability — a conditional at the recording site is one that
///        never leaves, per `06` §1's note on the capability set.
/// note  ⚠️ Every reading is sampled by the tick through `MeasureIndex` and never pushed. `86` §2.1: a producer
///        that pushed its own measure would write from inside a recording, contending with the tick for the
///        state the tick is presenting.
/// tag   owning
class HardwareMetrics
{
public:

    // 📝 Spans one rotation may declare. Sixteen carries `08` §3's thirteen recordings, the presentation and
    //    the whole-rotation span with room for one more, and the extent is sized against this once at bring-up
    //    rather than grown — a query extent that reallocated would invalidate a reading a rotation still writes.
    static constexpr std::uint32_t SpanLimit = 16u;   // [-] - declared spans per rotation

    // 📝 🔴 Two timestamps per span, one at each end, and the whole set is sized against the recording slot count —
    //    `06` §7's gate, arithmetic rather than a remark. A single rotation's worth would be read back while
    //    the next rotation was already overwriting it.
    static constexpr std::uint32_t TimestampsPerSlot = SpanLimit * 2u;   // [-]

    HardwareMetrics()                                  = default;
    HardwareMetrics(const HardwareMetrics&)            = delete;
    HardwareMetrics& operator=(const HardwareMetrics&) = delete;
    ~HardwareMetrics();

    /// 🧩 Constructs the timestamp extent every recorded span writes into, sized against the recording slot count.
    /// in    Exchange  [-]  the created device; borrowed and outlives this component
    /// out   Result   [-]  refuses with CapabilityAbsent when no device is active
    /// post  every span reads unavailable until one rotation has completed
    /// note  🔴 Delivers when `TimestampQueryAvailable` is false and constructs no extent. That is `08` §5's
    ///        substitution — metrics report unavailable, not zero — and refusing instead would make bring-up
    ///        fail on a device that can draw everything Slate draws.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> InspectHardware(const VulkanExchange& Exchange);

    /// 🧩 Declares one span of device execution by name, returning the ordinal that opens and closes it.
    /// in    SpanName  [-]  static text; the recording's own identity, so a reading names what it timed
    /// out   Result   [-]  refuses with ExtentExhausted above `SpanLimit`, and with ContentUnsupported for
    ///                      a name already declared — two spans of one name make one unreadable reading
    /// pre   no rotation is recording; every span is declared at bring-up
    /// note  🔴 Declared at bring-up like the recordings themselves. A span declared mid-run would have to grow
    ///        the query extent, and reallocating it invalidates the readings the standing rotations still hold.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare(const char* SpanName);

    /// 🧩 Records the timestamp that opens one declared span, and enters it on the nesting depth.
    /// in    Recorded      [-]  the recording being written into
    /// in    SlotIndex  [-]  which slot of the depth is standing
    /// in    SpanIndex   [-]  what `Declare` returned
    /// out   Result       [-]  refuses with ContentUnsupported for an undeclared ordinal or an excessive slot,
    ///                          and with RelationCyclic when the span is already open in this rotation
    /// post  the span's nesting depth is the count of spans standing open around it
    /// note  ⚠️ Delivers as a no-op without the capability, so the recording site is unconditional.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Open(VkCommandBuffer Recorded, std::uint32_t SlotIndex, std::uint32_t SpanIndex);

    /// 🧩 Records the timestamp that closes one declared span, and leaves it on the nesting depth.
    /// out   Result  [-]  refuses with ContentUnsupported for an undeclared ordinal or an excessive slot, and
    ///                     with RelationCyclic when the span was not opened in this rotation
    /// note  🔴 Closed in the reverse order it was opened. A span closed while a span opened inside it is still
    ///        standing has crossed the nesting, and the depth it reports then belongs to neither of them.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Close(VkCommandBuffer Recorded, std::uint32_t SlotIndex, std::uint32_t SpanIndex);

    /// 🧩 Clears one cycle slot's timestamps, immediately before the recording that writes them.
    /// out   Result  [-]  refuses with ContentUnsupported for an excessive slot
    /// pre   🔴 `CycleScheduler::Await` delivered for this slot — the device no longer reads its timestamps
    /// note  🔴 Cleared before the recording rather than after the readback. An uncleared timestamp reads as
    ///        whatever the previous rotation wrote, which is a plausible duration attributed to the wrong
    ///        rotation — the one failure a metric cannot be caught in, because nothing about it looks wrong.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Clear(VkCommandBuffer Recorded, std::uint32_t SlotIndex);

    /// 🧩 Reads back one completed rotation's timestamps and resolves each declared span's duration.
    /// in    SlotIndex   [-]  a slot whose completion has been awaited
    /// in    CompletedCount [-]  which rotation the readings belong to, for the lag the reading carries
    /// out   Result        [-]  refuses with ContentUnsupported for an excessive slot, HostDenied when the
    ///                           device declines the readback, and DeviceLost when the device was lost — a
    ///                           measurement reports the loss rather than absorbing it as absent data
    /// pre   🔴 the rotation that recorded into this slot has completed
    /// note  ⚠️ A span the rotation declared but never recorded reads unavailable rather than zero. That is the
    ///        conditional recording of `28`, whose atmosphere spans rebuild only on change — and a zero there
    ///        would report the rebuild as free rather than as absent.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Resolve(std::uint32_t SlotIndex, std::uint64_t CompletedCount);

    /// 🧩 One declared span's last resolved reading.
    /// out   Result  [-]  refuses with ContentUnsupported for an undeclared ordinal
    /// note  The delivered span may itself read unavailable. The refusal says the span was never declared; the
    ///       member says it was declared and has no reading — two different facts, and `86` presents both.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<MeasuredSpan> Current(std::uint32_t SpanIndex) const;

    /// 🧩 Declares every resolved reading into the register the tick samples.
    /// in    Sampled   [-]  where the readings are declared; borrowed for the call alone
    /// in    Arrival   [ns] when the tick took the sample
    /// note  🔴 Called from the tick and by nothing else — `86` §2.1. A magnitude is declared for an available
    ///        span and nothing at all is declared for an unavailable one, so `MeasureIndex::Resolve` refuses
    ///        rather than reading zero, which is `08` §5 enforced at the presentation rather than promised.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(MeasureIndex& Sampled, TickPoint Arrival) const;

    /// 🧩 Whether the device declared the timestamp capability at all.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Measuring() const;

    /// 🧩 How many spans are declared.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t DeclaredCount() const;

    /// 🧩 Destroys the timestamp extent and forgets every declared span.
    /// pre   the device is idle
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

private:

    /// 🧩 One declared span — its name, where its two timestamps sit, and what it last read.
    struct DeclaredSpan
    {
        const char*    Declared     = "";      // [-] - static text naming the span
        MeasuredSpan   LastReading  = {};      // [-] - resolved from the last completed rotation
        std::uint32_t  NestingDepth = 0u;      // [-] - spans standing open around it when it opened
    };

    /// 🧩 What one cycle slot recorded — which spans were opened and closed in it.
    struct RecordedSlot
    {
        bool  SpanOpened[SpanLimit] = {};   // [-] - the opening timestamp was recorded
        bool  SpanClosed[SpanLimit] = {};   // [-] - the closing timestamp was recorded
    };

    /// 🧩 Where one span's opening timestamp sits in the extent, for one cycle slot.
    /// note  Both timestamps of one span are adjacent, so the readback reads one rotation's whole run at once.
    static std::uint32_t TimestampIndexOf(std::uint32_t SlotIndex, std::uint32_t SpanIndex);

    const VulkanExchange*          DeviceEdge          = nullptr;         // [-]  - borrowed; never owned
    VkQueryPool                    TimestampExtent     = VK_NULL_HANDLE;  // [-]  - the vendor spelling, verbatim
    std::vector<DeclaredSpan>      DeclaredSpans       = {};              // [-]  - at most SpanLimit entries
    std::vector<RecordedSlot>  RecordedSlots           = {};              // [-]  - RecordingSlotCount entries
    std::uint32_t                  CurrentNesting     = 0u;              // [-]  - spans open in the recording now
    double                         TimestampToDuration = 0.0;             // [ms] - carried by one increment
    bool                           CapabilityHeld      = false;           // [-]  - the device declared timestamps
};

}   // namespace Slate
