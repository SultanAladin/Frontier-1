//============================================================================================================================================
//                                                               SPANSPACE.CPP
//============================================================================================================================================
// 🧩 The claim, the host write, the recorded transfer and the release of every linear device extent the engine holds.

#include "SlateVulkan/Device/SpanSpace/Api/SpanSpace.h"

#include <cstring>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

SpanSpace::~SpanSpace()
{
    Reclaim();
}

Deliver<bool> SpanSpace::ConstructSpanSpace(const VulkanExchange&      Exchange,
                                   ByteSpace&                 BackingSpace,
                                   const DiagnosticExtension& Naming)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge   = &Exchange;
    BackingBytes = &BackingSpace;
    NamingEdge   = &Naming;

    return Deliver<bool>::Result(true);
}

const char* SpanSpace::NameOf(SpanIntent Intent)
{
    // 📝 The intent rather than the claimant, because this component does not know who claimed. The ordinal
    //    the composed name carries is what distinguishes two spans of one intent, and the claimant resolves
    //    by that ordinal — so the driver's text and the caller's operand name the same thing.
    switch (Intent)
    {
        case SpanIntent::StorageRead:    return "SpanSpace storage span";
        case SpanIntent::StorageWritten: return "SpanSpace written storage span";
        case SpanIntent::UniformRead:    return "SpanSpace uniform span";
        case SpanIntent::IndirectRecord: return "SpanSpace indirect span";

        case SpanIntent::TransferSource:
        default:                         return "SpanSpace staging span";
    }
}

VkBufferUsageFlags SpanSpace::UsageOf(SpanIntent Intent)
{
    // 📝 Every intent accepts a transfer **into** the span, because a device-local span is written no other way.
    //    The transfer out is declared only where something reads back, which is the written storage alone.
    switch (Intent)
    {
        case SpanIntent::StorageRead:
            return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        case SpanIntent::StorageWritten:
            return VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                 | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        case SpanIntent::UniformRead:
            return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        // 🔴 An indirect span is also declared as storage, because the device writes the ordinals it later
        //    reads as a draw. `16` §2's culling records the surviving partitions into exactly this span, and a
        //    span carrying the indirect read alone is one the culling cannot write.
        case SpanIntent::IndirectRecord:
            return VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                 | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        case SpanIntent::TransferSource:
        default:
            return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE CLAIM
//------------------------------------------------------------------------------------------------------------------------

Deliver<SpanReservation> SpanSpace::Reserve(const SpanShape& Declared)
{
    if (DeviceEdge == nullptr || BackingBytes == nullptr)
        return Deliver<SpanReservation>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    if (Declared.SpanBytes == 0u)
        return Deliver<SpanReservation>::Refuse({ RefusalReason::ContentUnsupported, "a span of zero bytes" });

    if (Declared.Intent == SpanIntent::IntentCount)
        return Deliver<SpanReservation>::Refuse({ RefusalReason::ContentUnsupported, "no such span intent" });

    if (Declared.Residency == ExtentResidency::ResidencyCount)
        return Deliver<SpanReservation>::Refuse({ RefusalReason::ContentUnsupported, "no such residency" });

    const VkDevice Active = DeviceEdge->ActiveDevice();

    VkBufferCreateInfo SpanDeclaration = {};
    SpanDeclaration.sType              = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    SpanDeclaration.size               = Declared.SpanBytes;
    SpanDeclaration.usage              = UsageOf(Declared.Intent);
    SpanDeclaration.sharingMode        = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer Incoming = VK_NULL_HANDLE;

    if (vkCreateBuffer(Active, &SpanDeclaration, nullptr, &Incoming) != VK_SUCCESS)
        return Deliver<SpanReservation>::Refuse({ RefusalReason::ExtentExhausted, "the device rejected the span" });

    // 📝 Read from the created span and never computed from the shape, for `ImageSpace`'s reason: the alignment
    //    a uniform read requires is the vendor's declaration and a computed figure is right on one driver.
    VkMemoryRequirements Required = {};
    vkGetBufferMemoryRequirements(Active, Incoming, &Required);

    // 🔴 Committed rather than discretionary. A span is the working set of whichever document claimed it, and
    //    `06` §7 makes exhaustion of a committed claim a reported failure rather than residency policy.
    const Deliver<ByteReservation> Backing = BackingBytes->Reserve(Required.size,
                                                          Required.alignment,
                                                          Declared.Residency,
                                                          ReservationCondition::Committed);

    if (!Backing.Resolved)
    {
        vkDestroyBuffer(Active, Incoming, nullptr);
        return Deliver<SpanReservation>::Refuse(Backing.Error);
    }

    const ByteReservation Sliced = Backing.Resolve();

    if (vkBindBufferMemory(Active, Incoming, Sliced.BackingExtent, Sliced.ByteOffset) != VK_SUCCESS)
    {
        BackingBytes->Release(Sliced);
        vkDestroyBuffer(Active, Incoming, nullptr);

        return Deliver<SpanReservation>::Refuse(
            { RefusalReason::ContentUnsupported, "the device failed to bind the claimed bytes to the span" });
    }

    HeldSpan Taken;
    Taken.Extent       = Incoming;
    Taken.Backing      = Sliced;
    Taken.Shape        = Declared;
    Taken.SlotOccupied = true;

    // 📝 A released slot is reused rather than erased, so that an ordinal a contributing document recorded
    //    keeps naming what it named. Erasing renumbers every claim above it and nothing observes that.
    std::uint32_t SpanIndex = AbsentSpan;

    for (std::size_t Candidate = 0u; Candidate < Spans.size(); ++Candidate)
    {
        if (!Spans[Candidate].SlotOccupied)
        {
            Spans[Candidate] = Taken;
            SpanIndex      = static_cast<std::uint32_t>(Candidate);
            break;
        }
    }

    if (SpanIndex == AbsentSpan)
    {
        SpanIndex = static_cast<std::uint32_t>(Spans.size());
        Spans.push_back(Taken);
    }

    // 📝 🔴 `06` §7's diagnostic-name gate, named by the ordinal the claimant resolves the span by. The
    //    refusal is discarded for `ByteSpace`'s reason — a span that stands and could not be named is still
    //    the span the claimant asked for.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_BUFFER,
                        reinterpret_cast<std::uint64_t>(Incoming),
                        NameOf(Declared.Intent),
                        SpanIndex));

    SpanReservation Reserved;
    Reserved.Extent      = Incoming;
    Reserved.SpanBytes   = Declared.SpanBytes;
    Reserved.HostAddress = Sliced.HostAddress;
    Reserved.SpanIndex = SpanIndex;

    return Deliver<SpanReservation>::Result(Reserved);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE WRITES
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> SpanSpace::Amend(std::uint32_t  SpanIndex,
                               const void*    Incoming,
                               VkDeviceSize   IncomingBytes,
                               VkDeviceSize   ByteOffset)
{
    if (static_cast<std::size_t>(SpanIndex) >= Spans.size() || !Spans[SpanIndex].SlotOccupied)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no span stands at that ordinal" });

    HeldSpan& Held = Spans[SpanIndex];

    if (Held.Backing.HostAddress == nullptr)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a device-local span carries no host address to write through" });
    }

    if (Incoming == nullptr || IncomingBytes == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "nothing was supplied to write" });

    if (ByteOffset > Held.Shape.SpanBytes || IncomingBytes > Held.Shape.SpanBytes - ByteOffset)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the write would run past the claimed span" });

    std::memcpy(static_cast<unsigned char*>(Held.Backing.HostAddress) + ByteOffset,
                Incoming,
                static_cast<std::size_t>(IncomingBytes));

    return Deliver<bool>::Result(true);
}

Deliver<bool> SpanSpace::Transfer(VkCommandBuffer  Recorded,
                                  std::uint32_t    SourceIndex,
                                  std::uint32_t    TargetIndex,
                                  VkDeviceSize     TransferBytes)
{
    if (Recorded == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no recording was supplied" });

    if (static_cast<std::size_t>(SourceIndex) >= Spans.size() || !Spans[SourceIndex].SlotOccupied)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no source span stands at that ordinal" });

    if (static_cast<std::size_t>(TargetIndex) >= Spans.size() || !Spans[TargetIndex].SlotOccupied)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no target span stands at that ordinal" });

    const HeldSpan& Source = Spans[SourceIndex];
    const HeldSpan& Target = Spans[TargetIndex];

    // 📝 Zero reads as the whole of the source, which is what every staging site means. Spelling the source's
    //    own extent at each call site is one more place the two can disagree after a shape is amended.
    const VkDeviceSize Carried = TransferBytes == 0u ? Source.Shape.SpanBytes : TransferBytes;

    if (Carried > Source.Shape.SpanBytes || Carried > Target.Shape.SpanBytes)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the transfer would run past one of the spans" });

    VkBufferCopy Carrying = {};
    Carrying.srcOffset    = 0u;
    Carrying.dstOffset    = 0u;
    Carrying.size         = Carried;

    vkCmdCopyBuffer(Recorded, Source.Extent, Target.Extent, 1u, &Carrying);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE READS
//------------------------------------------------------------------------------------------------------------------------

Deliver<SpanReservation> SpanSpace::Current(std::uint32_t SpanIndex) const
{
    if (static_cast<std::size_t>(SpanIndex) >= Spans.size() || !Spans[SpanIndex].SlotOccupied)
        return Deliver<SpanReservation>::Refuse({ RefusalReason::ContentUnsupported, "no span stands at that ordinal" });

    const HeldSpan& Held = Spans[SpanIndex];

    SpanReservation Current;
    Current.Extent      = Held.Extent;
    Current.SpanBytes   = Held.Shape.SpanBytes;
    Current.HostAddress = Held.Backing.HostAddress;
    Current.SpanIndex = SpanIndex;

    return Deliver<SpanReservation>::Result(Current);
}

std::uint32_t SpanSpace::ReservedCount() const
{
    std::uint32_t Occupied = 0u;

    for (const HeldSpan& Held : Spans)
    {
        if (Held.SlotOccupied)
            ++Occupied;
    }

    return Occupied;
}

VkDeviceSize SpanSpace::ReservedBytes() const
{
    VkDeviceSize Reserved = 0u;

    for (const HeldSpan& Held : Spans)
    {
        if (Held.SlotOccupied)
            Reserved += Held.Shape.SpanBytes;
    }

    return Reserved;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void SpanSpace::Release(std::uint32_t SpanIndex)
{
    if (DeviceEdge == nullptr || static_cast<std::size_t>(SpanIndex) >= Spans.size())
        return;

    HeldSpan& Held = Spans[SpanIndex];

    if (!Held.SlotOccupied)
        return;

    vkDestroyBuffer(DeviceEdge->ActiveDevice(), Held.Extent, nullptr);

    if (BackingBytes != nullptr)
        BackingBytes->Release(Held.Backing);

    Held               = {};
    Held.SlotOccupied  = false;
}

void SpanSpace::Reclaim()
{
    for (std::size_t SpanIndex = 0u; SpanIndex < Spans.size(); ++SpanIndex)
        Release(static_cast<std::uint32_t>(SpanIndex));

    Spans.clear();
}

}   // namespace Slate
