//============================================================================================================================================
//                                                           HARDWAREMETRICS.CPP
//============================================================================================================================================
// 🧩 The timestamp extent, the recorded pair around each declared span, and the readback that reports or refuses.

#include "SlateVulkan/Device/HardwareMetrics/Api/HardwareMetrics.h"

#include <cstring>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> HardwareMetrics::InspectHardware(const VulkanExchange& Exchange)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;

    RecordedSlots.assign(RecordingSlotCount, RecordedSlot{});
    DeclaredSpans.clear();
    DeclaredSpans.reserve(SpanLimit);

    CurrentNesting     = 0u;
    TimestampToDuration = Exchange.Capability().TimestampToMilliseconds;

    // 📝 🔴 `08` §5's substitution, executed. A device that declares no timestamp capability constructs no
    //    extent, records nothing, and reports every span unavailable — and bring-up still delivers, because
    //    such a device draws everything Slate draws. Refusing here would have made a performance report a
    //    requirement for an image.
    if (!Exchange.Capability().TimestampQueryAvailable || TimestampToDuration <= 0.0)
    {
        CapabilityHeld = false;
        return Deliver<bool>::Result(true);
    }

    VkQueryPoolCreateInfo ExtentDeclaration = {};
    ExtentDeclaration.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    ExtentDeclaration.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    ExtentDeclaration.queryCount = TimestampsPerSlot * RecordingSlotCount;

    if (vkCreateQueryPool(Exchange.ActiveDevice(), &ExtentDeclaration, nullptr, &TimestampExtent) != VK_SUCCESS)
    {
        TimestampExtent = VK_NULL_HANDLE;
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the device rejected the timestamp extent" });
    }

    CapabilityHeld = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

std::uint32_t HardwareMetrics::TimestampIndexOf(std::uint32_t SlotIndex, std::uint32_t SpanIndex)
{
    return SlotIndex * TimestampsPerSlot + SpanIndex * 2u;
}

Deliver<std::uint32_t> HardwareMetrics::Declare(const char* SpanName)
{
    if (SpanName == nullptr || SpanName[0] == '\0')
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "a span was declared with no name" });

    if (static_cast<std::uint32_t>(DeclaredSpans.size()) >= SpanLimit)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "more spans were declared than the extent was sized against" });
    }

    // 📝 🔴 Two spans of one name make one unreadable reading: `MeasureIndex` is keyed by origin and quantity,
    //    so the second declaration would overwrite the first every time the tick sampled, and the report would
    //    name one span while carrying whichever duration happened to be declared last.
    for (const DeclaredSpan& Current : DeclaredSpans)
    {
        if (std::strcmp(Current.Declared, SpanName) == 0)
        {
            return Deliver<std::uint32_t>::Refuse(
                { RefusalReason::ContentUnsupported, "that span name is already declared" });
        }
    }

    DeclaredSpan Incoming;
    Incoming.Declared             = SpanName;
    Incoming.LastReading.Declared = SpanName;

    DeclaredSpans.push_back(Incoming);

    return Deliver<std::uint32_t>::Result(static_cast<std::uint32_t>(DeclaredSpans.size()) - 1u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> HardwareMetrics::Clear(VkCommandBuffer Recorded, std::uint32_t SlotIndex)
{
    if (!CapabilityHeld)
        return Deliver<bool>::Result(true);

    if (SlotIndex >= static_cast<std::uint32_t>(RecordedSlots.size()) || Recorded == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such cycle slot, or no recording" });

    // 🔴 The whole slot is cleared, including the spans this rotation will not record. An uncleared timestamp
    //    reads as whatever the previous rotation left there, and a stale duration attributed to this rotation is
    //    the one metric failure that looks entirely plausible.
    vkCmdResetQueryPool(Recorded, TimestampExtent, SlotIndex * TimestampsPerSlot, TimestampsPerSlot);

    RecordedSlots[SlotIndex] = RecordedSlot{};
    CurrentNesting         = 0u;

    return Deliver<bool>::Result(true);
}

Deliver<bool> HardwareMetrics::Open(VkCommandBuffer Recorded, std::uint32_t SlotIndex, std::uint32_t SpanIndex)
{
    if (!CapabilityHeld)
        return Deliver<bool>::Result(true);

    if (SlotIndex >= static_cast<std::uint32_t>(RecordedSlots.size()) ||
        SpanIndex  >= static_cast<std::uint32_t>(DeclaredSpans.size()) ||
        Recorded == VK_NULL_HANDLE)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such span, slot, or recording" });
    }

    if (RecordedSlots[SlotIndex].SpanOpened[SpanIndex])
        return Deliver<bool>::Refuse({ RefusalReason::RelationCyclic, "that span is already open in this rotation" });

    // 📝 The nesting depth is what stood open **around** this span when it opened, so the outermost span reads
    //    zero. `06` §1's "duration and depth": ⑤·i and ⑤·ii each nest inside whatever span wraps ⑤, and that is
    //    what says the two of them together account for what ⑤ costs.
    DeclaredSpans[SpanIndex].NestingDepth = CurrentNesting;
    ++CurrentNesting;

    RecordedSlots[SlotIndex].SpanOpened[SpanIndex] = true;

    // 🔴 The opening reading is taken at the **top** of the ordering and the closing one at the bottom. A pair
    //    both written at one end measures the interval between two recordings reaching the queue rather than the
    //    interval the device spent executing between them.
    vkCmdWriteTimestamp(Recorded,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        TimestampExtent,
                        TimestampIndexOf(SlotIndex, SpanIndex));

    return Deliver<bool>::Result(true);
}

Deliver<bool> HardwareMetrics::Close(VkCommandBuffer Recorded, std::uint32_t SlotIndex, std::uint32_t SpanIndex)
{
    if (!CapabilityHeld)
        return Deliver<bool>::Result(true);

    if (SlotIndex >= static_cast<std::uint32_t>(RecordedSlots.size()) ||
        SpanIndex  >= static_cast<std::uint32_t>(DeclaredSpans.size()) ||
        Recorded == VK_NULL_HANDLE)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such span, slot, or recording" });
    }

    if (!RecordedSlots[SlotIndex].SpanOpened[SpanIndex])
        return Deliver<bool>::Refuse({ RefusalReason::RelationCyclic, "that span was not opened in this rotation" });

    if (RecordedSlots[SlotIndex].SpanClosed[SpanIndex])
        return Deliver<bool>::Refuse({ RefusalReason::RelationCyclic, "that span is already closed" });

    // 🔴 Closed in the reverse order it was opened. A span closed while one opened inside it still stands has
    //    crossed the nesting, and the depth each of them reports then belongs to neither.
    if (CurrentNesting == 0u || DeclaredSpans[SpanIndex].NestingDepth != CurrentNesting - 1u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::RelationCyclic, "a span was closed while a span opened inside it still stands" });
    }

    --CurrentNesting;
    RecordedSlots[SlotIndex].SpanClosed[SpanIndex] = true;

    vkCmdWriteTimestamp(Recorded,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        TimestampExtent,
                        TimestampIndexOf(SlotIndex, SpanIndex) + 1u);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE READBACK
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> HardwareMetrics::Resolve(std::uint32_t SlotIndex, std::uint64_t CompletedCount)
{
    if (!CapabilityHeld)
        return Deliver<bool>::Result(true);

    if (SlotIndex >= static_cast<std::uint32_t>(RecordedSlots.size()))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such cycle slot" });

    if (DeclaredSpans.empty())
        return Deliver<bool>::Result(true);

    std::uint64_t Readings[TimestampsPerSlot] = {};

    // 📝 Read without `VK_QUERY_RESULT_WAIT_BIT`. The caller has already awaited this slot's completion, and a
    //    wait here would serialise the host against the device a second time for a reading it already has.
    //    `VK_NOT_READY` is therefore a defect in the caller's ordering rather than a slow device, and it is
    //    rejected rather than waited out.
    const VkResult ReadBack = vkGetQueryPoolResults(DeviceEdge->ActiveDevice(),
                                                    TimestampExtent,
                                                    SlotIndex * TimestampsPerSlot,
                                                    TimestampsPerSlot,
                                                    sizeof(Readings),
                                                    Readings,
                                                    sizeof(std::uint64_t),
                                                    VK_QUERY_RESULT_64_BIT);

    // 🔴 `06` §7: reported upward rather than absorbed. A measurement is the one caller with a reason to treat a
    //    lost device as merely unavailable data, and doing so would discard the report the recovery is waiting on.
    if (ReadBack == VK_ERROR_DEVICE_LOST)
        return Deliver<bool>::Refuse({ RefusalReason::DeviceLost, "the device was lost reading back timestamps" });

    if (ReadBack != VK_SUCCESS)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the device rejected the timestamp readback" });

    const RecordedSlot& Recorded = RecordedSlots[SlotIndex];

    for (std::uint32_t SpanIndex = 0u; SpanIndex < static_cast<std::uint32_t>(DeclaredSpans.size()); ++SpanIndex)
    {
        MeasuredSpan& Resolved = DeclaredSpans[SpanIndex].LastReading;

        // 🔴 A span the rotation declared but never recorded reports unavailable and keeps no previous
        //    duration. `28`'s atmosphere spans record only on change, and a retained duration would report a
        //    rebuild that did not happen — while a zero would report it as free. Both are `08` §5's defect.
        if (!Recorded.SpanOpened[SpanIndex] || !Recorded.SpanClosed[SpanIndex])
        {
            Resolved.Available    = false;
            Resolved.Duration     = 0.0;
            Resolved.RecordingRead = CompletedCount;
            continue;
        }

        const std::uint64_t Opened = Readings[SpanIndex * 2u];
        const std::uint64_t Closed = Readings[SpanIndex * 2u + 1u];

        // ⚠️ The closing reading may precede the opening one when the device's timestamp counter wrapped
        //    between the two. Reported unavailable rather than as the enormous duration the subtraction would
        //    produce, because an unsigned difference across a wrap is a plausible-looking number.
        if (Closed < Opened)
        {
            Resolved.Available    = false;
            Resolved.Duration     = 0.0;
            Resolved.RecordingRead = CompletedCount;
            continue;
        }

        Resolved.Duration     = static_cast<double>(Closed - Opened) * TimestampToDuration;
        Resolved.RecordingRead = CompletedCount;
        Resolved.Available    = true;
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE READINGS
//------------------------------------------------------------------------------------------------------------------------

Deliver<MeasuredSpan> HardwareMetrics::Current(std::uint32_t SpanIndex) const
{
    if (SpanIndex >= static_cast<std::uint32_t>(DeclaredSpans.size()))
        return Deliver<MeasuredSpan>::Refuse({ RefusalReason::ContentUnsupported, "that span was never declared" });

    return Deliver<MeasuredSpan>::Result(DeclaredSpans[SpanIndex].LastReading);
}

void HardwareMetrics::Report(MeasureIndex& Sampled, TickPoint Arrival) const
{
    for (const DeclaredSpan& Current : DeclaredSpans)
    {
        // 🔴 An unavailable span declares **nothing**, so `MeasureIndex::Resolve` refuses rather than reading
        //    zero. That is `08` §5 enforced at the presentation rather than promised in a comment: declaring a
        //    zero magnitude here would make an unmeasurable span indistinguishable from a free one.
        if (!Current.LastReading.Available)
            continue;

        Sampled.DeclareMagnitude("06 §6 HardwareMetrics", Current.Declared, Current.LastReading.Duration, Arrival);
    }
}

bool HardwareMetrics::Measuring() const
{
    return CapabilityHeld;
}

std::uint32_t HardwareMetrics::DeclaredCount() const
{
    return static_cast<std::uint32_t>(DeclaredSpans.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void HardwareMetrics::Reclaim()
{
    if (TimestampExtent != VK_NULL_HANDLE && DeviceEdge != nullptr &&
        DeviceEdge->ActiveDevice() != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(DeviceEdge->ActiveDevice(), TimestampExtent, nullptr);
    }

    TimestampExtent = VK_NULL_HANDLE;
    DeclaredSpans.clear();
    RecordedSlots.clear();
    CurrentNesting = 0u;
    CapabilityHeld  = false;
}

HardwareMetrics::~HardwareMetrics()
{
    Reclaim();
}

}   // namespace Slate
