//============================================================================================================================================
//                                                           DESCRIPTORINDEX.CPP
//============================================================================================================================================
// 🧩 The layout declaration that closes at bring-up, the extent it is sized against, and the per-slot write.

#include "SlateVulkan/Device/DescriptorIndex/Api/DescriptorIndex.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DescriptorIndex::ConstructDescriptorIndex(const VulkanExchange& Exchange, const DiagnosticExtension& Naming)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    NamingEdge = &Naming;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> DescriptorIndex::Declare(const std::vector<DescriptorSlot>& Declared)
{
    if (DeviceEdge == nullptr)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    // 🔴 `06` §7's first gate. A layout constructed after the extent was sized is one the extent was not sized
    //    for, and the recording that asked for it is the recording that stalls on the vendor constructing it.
    if (DeclarationFixed)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::RelationCyclic, "the declaration is closed; no layout is constructed after it" });
    }

    if (Declared.empty())
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "a layout declaring no slot" });

    for (std::size_t Index = 0u; Index < Declared.size(); ++Index)
    {
        if (Declared[Index].CarriedCount == 0u)
            return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "a slot carrying nothing" });

        for (std::size_t Against = Index + 1u; Against < Declared.size(); ++Against)
        {
            if (Declared[Index].SlotIndex == Declared[Against].SlotIndex)
            {
                return Deliver<std::uint32_t>::Refuse(
                    { RefusalReason::ContentUnsupported, "two slots declare the same ordinal" });
            }
        }
    }

    std::vector<VkDescriptorSetLayoutBinding> VendorDeclared;
    VendorDeclared.reserve(Declared.size());

    for (const DescriptorSlot& Slot : Declared)
    {
        VkDescriptorSetLayoutBinding Carried = {};
        Carried.binding                      = Slot.SlotIndex;
        Carried.descriptorType               = Slot.Carried;
        Carried.descriptorCount              = Slot.CarriedCount;
        Carried.stageFlags                   = Slot.ReachingStages;
        Carried.pImmutableSamplers           = nullptr;

        VendorDeclared.push_back(Carried);
    }

    VkDescriptorSetLayoutCreateInfo LayoutDeclaration = {};
    LayoutDeclaration.sType                           = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    LayoutDeclaration.bindingCount                    = static_cast<std::uint32_t>(VendorDeclared.size());
    LayoutDeclaration.pBindings                       = VendorDeclared.data();

    DeclaredLayout Incoming;
    Incoming.Slots = Declared;

    if (vkCreateDescriptorSetLayout(DeviceEdge->ActiveDevice(), &LayoutDeclaration, nullptr, &Incoming.Constructed)
        != VK_SUCCESS)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "the device rejected the declared layout" });
    }

    Layouts.push_back(Incoming);

    const std::uint32_t LayoutIndex = static_cast<std::uint32_t>(Layouts.size() - 1u);

    // 📝 🔴 `06` §7's diagnostic-name gate. The refusal is discarded for `ByteSpace`'s reason — a layout that
    //    stands and could not be named is still the layout every program is constructed against.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                        reinterpret_cast<std::uint64_t>(Incoming.Constructed),
                        "DescriptorIndex layout",
                        LayoutIndex));

    return Deliver<std::uint32_t>::Result(LayoutIndex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ONE EXTENT
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DescriptorIndex::Fix(std::uint32_t ConcurrentSets)
{
    if (DeviceEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    if (DeclarationFixed)
        return Deliver<bool>::Refuse({ RefusalReason::RelationCyclic, "the declaration is already closed" });

    if (Layouts.empty())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no layout was declared" });

    if (ConcurrentSets == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "an extent accepting no set" });

    // 📝 Every declared slot of every declared layout is counted once per accepted set, so the extent is sized
    //    for the worst arrangement of claims across the layouts rather than for one assumed distribution.
    //    Over-sizing costs descriptor bookkeeping the vendor keeps on the host; under-sizing refuses a claim
    //    at the rotation that needed it.
    std::vector<VkDescriptorPoolSize> Accepted;

    for (const DeclaredLayout& Holding : Layouts)
    {
        for (const DescriptorSlot& Slot : Holding.Slots)
        {
            const std::uint32_t Spanned = Slot.CarriedCount * ConcurrentSets;

            bool Folded = false;

            for (VkDescriptorPoolSize& Current : Accepted)
            {
                if (Current.type == Slot.Carried)
                {
                    Current.descriptorCount += Spanned;
                    Folded                    = true;
                    break;
                }
            }

            if (!Folded)
                Accepted.push_back({ Slot.Carried, Spanned });
        }
    }

    VkDescriptorPoolCreateInfo ExtentDeclaration = {};
    ExtentDeclaration.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ExtentDeclaration.maxSets                    = ConcurrentSets * RecordingSlotCount;
    ExtentDeclaration.poolSizeCount              = static_cast<std::uint32_t>(Accepted.size());
    ExtentDeclaration.pPoolSizes                 = Accepted.data();

    // 📝 No free-descriptor-set capability. Sets are returned by resetting the whole extent at teardown, which
    //    is the only point at which none of them is read — an individually freed set is one whose slot the
    //    vendor reuses while a rotation still names it.
    ExtentDeclaration.flags = 0u;

    if (vkCreateDescriptorPool(DeviceEdge->ActiveDevice(), &ExtentDeclaration, nullptr, &DescriptorExtent)
        != VK_SUCCESS)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ExtentExhausted, "the device rejected the descriptor extent the declaration was sized to" });
    }

    DeclarationFixed = true;

    // 📝 🔴 `06` §7's gate. Named by the two-operand form and carrying no ordinal, because there is exactly one
    //    descriptor extent for the engine's whole life and an ordinal on a single object reads as one of many.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_DESCRIPTOR_POOL,
                        reinterpret_cast<std::uint64_t>(DescriptorExtent),
                        "DescriptorIndex descriptor extent"));

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CLAIM
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> DescriptorIndex::Reserve(std::uint32_t LayoutIndex)
{
    if (DeviceEdge == nullptr || !DeclarationFixed || DescriptorExtent == VK_NULL_HANDLE)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::CapabilityAbsent, "the declaration is not yet closed" });

    if (static_cast<std::size_t>(LayoutIndex) >= Layouts.size())
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no layout stands at that ordinal" });

    // 📝 🔴 `06` §2.1: one set per cycle slot, claimed together. Reserving them apart accepts a claim that
    //    half-succeeds, and the recording then writes rotation one against a set that was never sliced.
    const std::vector<VkDescriptorSetLayout> Repeated(RecordingSlotCount, Layouts[LayoutIndex].Constructed);

    VkDescriptorSetAllocateInfo SetDeclaration = {};
    SetDeclaration.sType                       = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    SetDeclaration.descriptorPool              = DescriptorExtent;
    SetDeclaration.descriptorSetCount          = RecordingSlotCount;
    SetDeclaration.pSetLayouts                 = Repeated.data();

    ReservedSet Incoming;
    Incoming.LayoutIndex = LayoutIndex;
    Incoming.PerSlot.assign(RecordingSlotCount, VK_NULL_HANDLE);

    if (vkAllocateDescriptorSets(DeviceEdge->ActiveDevice(), &SetDeclaration, Incoming.PerSlot.data())
        != VK_SUCCESS)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ExtentExhausted, "the descriptor extent accepts no further set" });
    }

    Reserved.push_back(Incoming);

    const std::uint32_t ReservationIndex = static_cast<std::uint32_t>(Reserved.size() - 1u);

    // 📝 🔴 `06` §7's gate. A set is addressed by a claim and a cycle slot, and the name carries the two
    //    flattened in the order the depth fixes — so a claim's sets sort adjacently in the driver's text and the
    //    reader recovers the pair the same way `Resolve` reaches the set. Naming by the claim alone would give
    //    every cycle slot of one claim a single name, and a set amended in the wrong slot is exactly the
    //    defect the depth exists to catch.
    for (std::uint32_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
    {
        Discard(NamingEdge->Declare(VK_OBJECT_TYPE_DESCRIPTOR_SET,
                            reinterpret_cast<std::uint64_t>(Incoming.PerSlot[SlotIndex]),
                            "DescriptorIndex set",
                            ReservationIndex * RecordingSlotCount + SlotIndex));
    }

    return Deliver<std::uint32_t>::Result(ReservationIndex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE WRITE
//------------------------------------------------------------------------------------------------------------------------

const DescriptorSlot* DescriptorIndex::SlotOf(const DeclaredLayout& Holding, std::uint32_t SlotIndex) const
{
    for (const DescriptorSlot& Slot : Holding.Slots)
    {
        if (Slot.SlotIndex == SlotIndex)
            return &Slot;
    }

    return nullptr;
}

Deliver<bool> DescriptorIndex::Amend(std::uint32_t                          ReservationIndex,
                                     std::uint32_t                          SlotIndex,
                                     const std::vector<DescriptorContent>&  Amended)
{
    if (DeviceEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    if (static_cast<std::size_t>(ReservationIndex) >= Reserved.size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no claim stands at that ordinal" });

    if (SlotIndex >= RecordingSlotCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the cycle slot is outside the depth" });

    if (Amended.empty())
        return Deliver<bool>::Result(true);

    const ReservedSet&     Current = Reserved[ReservationIndex];
    const DeclaredLayout& Holding  = Layouts[Current.LayoutIndex];

    // 📝 The two content declarations are held alongside the writes rather than inside the loop, because the
    //    vendor reads them at the call and a pointer into a temporary is a read of what the stack now holds.
    // 🔴 Both are reserved to the whole amendment before the first entry, so that no push moves an entry a
    //    write already points at. Without the reservation the addresses taken below are addresses into a run
    //    the next entry reallocates, and the vendor then reads the content of a freed span.
    std::vector<VkWriteDescriptorSet>    Writes;
    std::vector<VkDescriptorBufferInfo>  SpanContent;
    std::vector<VkDescriptorImageInfo>   ImageContent;

    Writes.reserve(Amended.size());
    SpanContent.reserve(Amended.size());
    ImageContent.reserve(Amended.size());

    for (const DescriptorContent& Content : Amended)
    {
        const DescriptorSlot* Declared = SlotOf(Holding, Content.SlotIndex);

        if (Declared == nullptr)
            return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the layout declares no such slot" });

        VkWriteDescriptorSet Written = {};
        Written.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        Written.dstSet               = Current.PerSlot[SlotIndex];
        Written.dstBinding           = Content.SlotIndex;
        Written.dstArrayElement      = 0u;
        Written.descriptorCount      = 1u;
        Written.descriptorType       = Declared->Carried;

        switch (Declared->Carried)
        {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            {
                if (Content.SpanExtent == VK_NULL_HANDLE)
                {
                    return Deliver<bool>::Refuse(
                        { RefusalReason::ContentUnsupported, "a span slot written with no span" });
                }

                SpanContent.push_back({ Content.SpanExtent, Content.SpanOffset, Content.SpanBytes });

                Written.pBufferInfo = &SpanContent.back();
                Written.pImageInfo  = nullptr;
                break;
            }

            default:
            {
                if (Content.ImageView == VK_NULL_HANDLE)
                {
                    return Deliver<bool>::Refuse(
                        { RefusalReason::ContentUnsupported, "an image slot written with no view" });
                }

                ImageContent.push_back({ Content.ImageSampler, Content.ImageView, Content.ImageCurrent });

                Written.pImageInfo  = &ImageContent.back();
                Written.pBufferInfo = nullptr;
                break;
            }
        }

        Writes.push_back(Written);
    }

    vkUpdateDescriptorSets(DeviceEdge->ActiveDevice(),
                           static_cast<std::uint32_t>(Writes.size()),
                           Writes.data(),
                           0u,
                           nullptr);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT IS DECLARED
//------------------------------------------------------------------------------------------------------------------------

Deliver<VkDescriptorSet> DescriptorIndex::Resolve(std::uint32_t ReservationIndex, std::uint32_t SlotIndex) const
{
    if (static_cast<std::size_t>(ReservationIndex) >= Reserved.size())
        return Deliver<VkDescriptorSet>::Refuse({ RefusalReason::ContentUnsupported, "no claim stands at that ordinal" });

    if (SlotIndex >= RecordingSlotCount)
    {
        return Deliver<VkDescriptorSet>::Refuse(
            { RefusalReason::ContentUnsupported, "the cycle slot is outside the depth" });
    }

    return Deliver<VkDescriptorSet>::Result(Reserved[ReservationIndex].PerSlot[SlotIndex]);
}

Deliver<VkDescriptorSetLayout> DescriptorIndex::Layout(std::uint32_t LayoutIndex) const
{
    if (static_cast<std::size_t>(LayoutIndex) >= Layouts.size())
    {
        return Deliver<VkDescriptorSetLayout>::Refuse(
            { RefusalReason::ContentUnsupported, "no layout stands at that ordinal" });
    }

    return Deliver<VkDescriptorSetLayout>::Result(Layouts[LayoutIndex].Constructed);
}

std::uint32_t DescriptorIndex::DeclaredCount() const
{
    return static_cast<std::uint32_t>(Layouts.size());
}

std::uint32_t DescriptorIndex::ReservedCount() const
{
    return static_cast<std::uint32_t>(Reserved.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void DescriptorIndex::Reclaim()
{
    if (DeviceEdge != nullptr && DeviceEdge->ActiveDevice() != VK_NULL_HANDLE)
    {
        const VkDevice Active = DeviceEdge->ActiveDevice();

        // 📝 The extent before the layouts, and every set with the extent. A set outliving the extent it was
        //    sliced from is a vendor reference into an allocation the driver has already reclaimed.
        if (DescriptorExtent != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Active, DescriptorExtent, nullptr);
            DescriptorExtent = VK_NULL_HANDLE;
        }

        for (DeclaredLayout& Holding : Layouts)
        {
            if (Holding.Constructed != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(Active, Holding.Constructed, nullptr);
                Holding.Constructed = VK_NULL_HANDLE;
            }
        }
    }

    Reserved.clear();
    Layouts.clear();
    DeclarationFixed = false;
}

DescriptorIndex::~DescriptorIndex()
{
    Reclaim();
}

}   // namespace Slate
