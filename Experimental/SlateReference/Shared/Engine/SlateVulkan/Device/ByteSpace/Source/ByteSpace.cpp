//============================================================================================================================================
//                                                              BYTESPACE.CPP
//============================================================================================================================================
// 🧩 Residency scoring, the first-fit slice, and the coalescing release that keeps an extent from fragmenting away.

#include "SlateVulkan/Device/ByteSpace/Api/ByteSpace.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 ALIGNMENT ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 An alignment of zero reads as one so that a caller with no declared requirement need not spell a
    //    literal. Every other value is required to be a power of two, which the vendor guarantees for every
    //    requirement it reports — a caller passing something else has computed it rather than read it.
    constexpr bool PowerOfTwo(VkDeviceSize Candidate)
    {
        return Candidate != 0u && (Candidate & (Candidate - 1u)) == 0u;
    }

    constexpr VkDeviceSize RaiseToAlignment(VkDeviceSize Offset, VkDeviceSize Alignment)
    {
        return (Offset + Alignment - 1u) & ~(Alignment - 1u);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ByteSpace::ReserveByteSpace(const VulkanExchange& Exchange, const DiagnosticExtension& Naming)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE || Exchange.ScoredDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    NamingEdge = &Naming;

    vkGetPhysicalDeviceMemoryProperties(Exchange.ScoredDevice(), &VendorDeclared);

    // 📝 🔴 A host-writable span that is flushed must be flushed on this granularity, and a span sharing an
    //    atom with another claimant is one whose neighbour is flushed along with it. Aligning every claim to
    //    the atom costs a few bytes per claim and removes the whole class of defect.
    VkPhysicalDeviceProperties DeviceDeclaration = {};
    vkGetPhysicalDeviceProperties(Exchange.ScoredDevice(), &DeviceDeclaration);

    NonCoherentAtom = DeviceDeclaration.limits.nonCoherentAtomSize;

    if (NonCoherentAtom == 0u)
        NonCoherentAtom = 1u;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  RESIDENCY SCORING
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ByteSpace::ClassifyResidency(ExtentResidency Residency) const
{
    VkMemoryPropertyFlags Required = 0u;

    if (Residency == ExtentResidency::DeviceLocal)
    {
        Required = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    else
    {
        // 📝 Coherent as well as visible. The alternative is an explicit flush at every write site, and a
        //    write site that forgets one produces content the device reads as it stood one rotation ago —
        //    which the artist meets as a stroke that appears late rather than as a synchronisation defect.
        Required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }

    // 📝 The vendor declares its entries in preference order, so the first entry carrying every required
    //    property is the one to take. Scoring further would mean preferring a heap the vendor did not.
    for (std::uint32_t Index = 0u; Index < VendorDeclared.memoryTypeCount; ++Index)
    {
        const VkMemoryPropertyFlags Carried = VendorDeclared.memoryTypes[Index].propertyFlags;

        if ((Carried & Required) == Required)
            return Deliver<std::uint32_t>::Result(Index);
    }

    return Deliver<std::uint32_t>::Refuse(
        { RefusalReason::CapabilityAbsent, "no declared entry carries the residency the claim asked for" });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  EXTENT ACQUISITION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ByteSpace::ConstructExtent(ExtentResidency Residency, VkDeviceSize MinimumBytes)
{
    const Deliver<std::uint32_t> VendorIndex = ClassifyResidency(Residency);

    if (!VendorIndex.Resolved)
        return Deliver<std::uint32_t>::Refuse(VendorIndex.Error);

    VkDeviceSize ExtentBytes = Residency == ExtentResidency::DeviceLocal
                                 ? DeviceLocalExtentBytes
                                 : HostWritableExtentBytes;

    // 📝 A claim larger than the standard piece takes a piece of its own rather than being rejected. `20`'s
    //    residency extent and `16`'s partition spans are both sized by the document rather than by this
    //    file, and refusing them here would put a limit on the document in the allocator.
    if (MinimumBytes > ExtentBytes)
        ExtentBytes = MinimumBytes;

    // 🔴 `06` §7: the largest single allocation the device accepts is scored at creation. Exceeding it is a
    //    refusal here rather than a vendor error at the allocation, which reports as a driver failure with
    //    no operand named.
    const std::uint64_t LargestReservation = DeviceEdge->Capability().LargestExtentReservation;

    if (LargestReservation != 0u && static_cast<std::uint64_t>(ExtentBytes) > LargestReservation)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "the span exceeds the largest allocation the device accepts" });
    }

    VkMemoryAllocateInfo ExtentDeclaration = {};
    ExtentDeclaration.sType                = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ExtentDeclaration.allocationSize       = ExtentBytes;
    ExtentDeclaration.memoryTypeIndex      = VendorIndex.Resolve();

    SlicedExtent Incoming;
    Incoming.TotalBytes    = ExtentBytes;
    Incoming.Residency     = Residency;
    Incoming.VendorIndex = VendorIndex.Resolve();

    if (vkAllocateMemory(DeviceEdge->ActiveDevice(), &ExtentDeclaration, nullptr, &Incoming.DeviceExtent) != VK_SUCCESS)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "the device rejected a further byte extent" });
    }

    // 📝 🔴 Mapped once for the extent's whole life, never per claim. Mapping is not reference-counted by
    //    every vendor, so two claims mapping the same extent and one of them unmapping leaves the other
    //    holding an address that is no longer valid.
    if (Residency == ExtentResidency::HostWritable)
    {
        if (vkMapMemory(DeviceEdge->ActiveDevice(), Incoming.DeviceExtent, 0u, VK_WHOLE_SIZE, 0u,
                        &Incoming.HostAddress) != VK_SUCCESS)
        {
            vkFreeMemory(DeviceEdge->ActiveDevice(), Incoming.DeviceExtent, nullptr);
            return Deliver<std::uint32_t>::Refuse(
                { RefusalReason::ExtentExhausted, "the device failed to map a host-writable extent" });
        }
    }

    Incoming.Unclaimed.push_back({ 0u, ExtentBytes });

    Extents.push_back(Incoming);

    const std::uint32_t ExtentIndex = static_cast<std::uint32_t>(Extents.size() - 1u);

    // 📝 🔴 `06` §7's diagnostic-name gate. The refusal is discarded deliberately: an extent that stands and
    //    could not be named is still an extent every later claim is sliced from, and refusing the allocation
    //    over a name would make the diagnostic capability a requirement for drawing anything.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_DEVICE_MEMORY,
                        reinterpret_cast<std::uint64_t>(Incoming.DeviceExtent),
                        Residency == ExtentResidency::DeviceLocal ? "ByteSpace device-local extent"
                                                                  : "ByteSpace host-writable extent",
                        ExtentIndex));

    return Deliver<std::uint32_t>::Result(ExtentIndex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE SLICE
//------------------------------------------------------------------------------------------------------------------------

Deliver<ByteReservation> ByteSpace::Reserve(VkDeviceSize    RequestedBytes,
                                    VkDeviceSize    ByteAlignment,
                                    ExtentResidency Residency,
                                    ReservationCondition   Condition)
{
    if (DeviceEdge == nullptr)
        return Deliver<ByteReservation>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    if (RequestedBytes == 0u)
        return Deliver<ByteReservation>::Refuse({ RefusalReason::ContentUnsupported, "a claim of zero bytes" });

    if (Residency == ExtentResidency::ResidencyCount)
        return Deliver<ByteReservation>::Refuse({ RefusalReason::ContentUnsupported, "no such residency" });

    VkDeviceSize Alignment = ByteAlignment == 0u ? 1u : ByteAlignment;

    if (!PowerOfTwo(Alignment))
        return Deliver<ByteReservation>::Refuse({ RefusalReason::ContentUnsupported, "the alignment is not a power of two" });

    if (Residency == ExtentResidency::HostWritable && Alignment < NonCoherentAtom)
        Alignment = NonCoherentAtom;

    // 📝 First fit across the extents already held, then one further extent, then the refusal. Best fit was
    //    not chosen: it costs a full scan per claim to save fragmentation this arrangement does not suffer,
    //    because the claim sizes here are a handful of shapes repeated rather than an arbitrary spread.
    for (int Attempt = 0; Attempt < 2; ++Attempt)
    {
        for (std::size_t ExtentIndex = 0u; ExtentIndex < Extents.size(); ++ExtentIndex)
        {
            SlicedExtent& Candidate = Extents[ExtentIndex];

            if (Candidate.Residency != Residency)
                continue;

            for (std::size_t SpanIndex = 0u; SpanIndex < Candidate.Unclaimed.size(); ++SpanIndex)
            {
                const FreeSpan     Available  = Candidate.Unclaimed[SpanIndex];
                const VkDeviceSize Raised     = RaiseToAlignment(Available.ByteOffset, Alignment);
                const VkDeviceSize Introduced = Raised - Available.ByteOffset;

                if (Introduced > Available.ByteSpan || Available.ByteSpan - Introduced < RequestedBytes)
                    continue;

                // 📝 The alignment padding stays in the free list as its own span rather than being folded
                //    into the claim. Folding it makes ReservedBytes report padding as claimed content, and
                //    `86`'s residency figure then drifts upward for a reason no reader can attribute.
                const VkDeviceSize Trailing = Available.ByteSpan - Introduced - RequestedBytes;

                const std::ptrdiff_t Reinsertion = static_cast<std::ptrdiff_t>(SpanIndex);

                Candidate.Unclaimed.erase(Candidate.Unclaimed.begin() + Reinsertion);

                // 📝 The trailing span is inserted first and the leading one ahead of it, so the ascending
                //    offset order the release path relies on survives the split without a re-sort.
                if (Trailing != 0u)
                {
                    Candidate.Unclaimed.insert(Candidate.Unclaimed.begin() + Reinsertion,
                                               { Raised + RequestedBytes, Trailing });
                }

                if (Introduced != 0u)
                {
                    Candidate.Unclaimed.insert(Candidate.Unclaimed.begin() + Reinsertion,
                                               { Available.ByteOffset, Introduced });
                }

                Candidate.TakenBytes += RequestedBytes;

                ByteReservation Sliced;
                Sliced.BackingExtent = Candidate.DeviceExtent;
                Sliced.ByteOffset    = Raised;
                Sliced.ByteSpan      = RequestedBytes;
                Sliced.ExtentIndex = static_cast<std::uint32_t>(ExtentIndex);
                Sliced.HostAddress   = Candidate.HostAddress == nullptr
                                         ? nullptr
                                         : static_cast<void*>(static_cast<unsigned char*>(Candidate.HostAddress) + Raised);

                return Deliver<ByteReservation>::Result(Sliced);
            }
        }

        if (Attempt == 0)
        {
            const Deliver<std::uint32_t> Further = ConstructExtent(Residency, RaiseToAlignment(RequestedBytes, Alignment));

            if (!Further.Resolved)
                break;
        }
    }

    // 🔴 `06` §7: discretionary exhaustion is residency policy and not a reported failure, so the reason is
    //    the same and the operand names which it was. `20` refuses a promotion on it and evicts; a committed
    //    claimant reports it through `86`. Neither branch is decided here.
    if (Condition == ReservationCondition::Discretionary)
    {
        return Deliver<ByteReservation>::Refuse(
            { RefusalReason::ExtentExhausted, "a discretionary claim found no span; eviction is the caller's" });
    }

    return Deliver<ByteReservation>::Refuse(
        { RefusalReason::ExtentExhausted, "no byte extent satisfies the claim, and no further one was granted" });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void ByteSpace::Release(const ByteReservation& Reserved)
{
    if (Reserved.ExtentIndex == AbsentExtent || Reserved.ByteSpan == 0u)
        return;

    if (static_cast<std::size_t>(Reserved.ExtentIndex) >= Extents.size())
        return;

    SlicedExtent& Holding = Extents[Reserved.ExtentIndex];

    // 📝 Inserted in offset order and then coalesced with both neighbours. Without the coalescing an extent
    //    that has cycled through a few thousand claims holds its whole span in the free list and satisfies
    //    none of them — the bytes are free and the allocator cannot see that they adjoin.
    std::size_t Insertion = 0u;

    while (Insertion < Holding.Unclaimed.size() && Holding.Unclaimed[Insertion].ByteOffset < Reserved.ByteOffset)
        ++Insertion;

    Holding.Unclaimed.insert(Holding.Unclaimed.begin() + static_cast<std::ptrdiff_t>(Insertion),
                             { Reserved.ByteOffset, Reserved.ByteSpan });

    Holding.TakenBytes -= Reserved.ByteSpan;

    if (Insertion + 1u < Holding.Unclaimed.size())
    {
        FreeSpan&       Returned  = Holding.Unclaimed[Insertion];
        const FreeSpan& Following = Holding.Unclaimed[Insertion + 1u];

        if (Returned.ByteOffset + Returned.ByteSpan == Following.ByteOffset)
        {
            Returned.ByteSpan += Following.ByteSpan;
            Holding.Unclaimed.erase(Holding.Unclaimed.begin() + static_cast<std::ptrdiff_t>(Insertion + 1u));
        }
    }

    if (Insertion > 0u)
    {
        FreeSpan&       Preceding = Holding.Unclaimed[Insertion - 1u];
        const FreeSpan& Returned  = Holding.Unclaimed[Insertion];

        if (Preceding.ByteOffset + Preceding.ByteSpan == Returned.ByteOffset)
        {
            Preceding.ByteSpan += Returned.ByteSpan;
            Holding.Unclaimed.erase(Holding.Unclaimed.begin() + static_cast<std::ptrdiff_t>(Insertion));
        }
    }
}

void ByteSpace::Reclaim()
{
    if (DeviceEdge == nullptr || DeviceEdge->ActiveDevice() == VK_NULL_HANDLE)
    {
        Extents.clear();
        return;
    }

    for (SlicedExtent& Held : Extents)
    {
        if (Held.HostAddress != nullptr)
        {
            vkUnmapMemory(DeviceEdge->ActiveDevice(), Held.DeviceExtent);
            Held.HostAddress = nullptr;
        }

        if (Held.DeviceExtent != VK_NULL_HANDLE)
        {
            vkFreeMemory(DeviceEdge->ActiveDevice(), Held.DeviceExtent, nullptr);
            Held.DeviceExtent = VK_NULL_HANDLE;
        }
    }

    Extents.clear();
}

ByteSpace::~ByteSpace()
{
    Reclaim();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS HELD
//------------------------------------------------------------------------------------------------------------------------

VkDeviceSize ByteSpace::ReservedBytes(ExtentResidency Residency) const
{
    VkDeviceSize Taken = 0u;

    for (const SlicedExtent& Held : Extents)
    {
        if (Held.Residency == Residency)
            Taken += Held.TakenBytes;
    }

    return Taken;
}

VkDeviceSize ByteSpace::BackingBytes(ExtentResidency Residency) const
{
    VkDeviceSize Backing = 0u;

    for (const SlicedExtent& Held : Extents)
    {
        if (Held.Residency == Residency)
            Backing += Held.TotalBytes;
    }

    return Backing;
}

std::uint32_t ByteSpace::ExtentCount() const
{
    return static_cast<std::uint32_t>(Extents.size());
}

}   // namespace Slate
