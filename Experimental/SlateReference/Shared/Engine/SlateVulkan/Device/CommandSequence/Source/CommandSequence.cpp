//============================================================================================================================================
//                                                           COMMANDSEQUENCE.CPP
//============================================================================================================================================
// 🧩 The per-slot recording extents, the open that resets one whole, and the surrender to the one graphics queue.

#include "SlateVulkan/Device/CommandSequence/Api/CommandSequence.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> CommandSequence::ConstructCommandSequence(const VulkanExchange& Exchange, const DiagnosticExtension& Naming)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    NamingEdge = &Naming;

    const VkDevice Active = Exchange.ActiveDevice();

    VkCommandPoolCreateInfo ExtentDeclaration = {};
    ExtentDeclaration.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ExtentDeclaration.queueFamilyIndex        = Exchange.Capability().GraphicsFamilyIndex;

    // 📝 Transient rather than individually resettable. Every recording of a slot is rewritten from nothing
    //    each rotation, which is what the vendor's transient arrangement is for; the resettable one asks it to
    //    keep per-recording bookkeeping no path here ever reads.
    ExtentDeclaration.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    Slots.assign(RecordingSlotCount, RecordingSlot{});

    // 📝 Walked by ordinal rather than by reference, because the cycle slot is what each object is named by
    //    and it is the same ordinal every later call addresses the slot with.
    for (std::uint32_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
    {
        RecordingSlot& Slot = Slots[SlotIndex];

        if (vkCreateCommandPool(Active, &ExtentDeclaration, nullptr, &Slot.RecordingExtent) != VK_SUCCESS)
        {
            Reclaim();
            return Deliver<bool>::Refuse(
                { RefusalReason::ExtentExhausted, "the device rejected a recording extent of the rotation" });
        }

        VkCommandBufferAllocateInfo RecordingDeclaration = {};
        RecordingDeclaration.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        RecordingDeclaration.commandPool                 = Slot.RecordingExtent;
        RecordingDeclaration.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        RecordingDeclaration.commandBufferCount          = 1u;

        if (vkAllocateCommandBuffers(Active, &RecordingDeclaration, &Slot.Primary) != VK_SUCCESS)
        {
            Reclaim();
            return Deliver<bool>::Refuse(
                { RefusalReason::ExtentExhausted, "the device rejected a recording of the rotation" });
        }

        // 📝 🔴 `06` §7's diagnostic-name gate. The refusals are discarded for `ByteSpace`'s reason — a slot
        //    that stands and could not be named is still the slot the rotation records into.
        Discard(NamingEdge->Declare(VK_OBJECT_TYPE_COMMAND_POOL,
                            reinterpret_cast<std::uint64_t>(Slot.RecordingExtent),
                            "CommandSequence rotation extent",
                            SlotIndex));

        Discard(NamingEdge->Declare(VK_OBJECT_TYPE_COMMAND_BUFFER,
                            reinterpret_cast<std::uint64_t>(Slot.Primary),
                            "CommandSequence rotation recording",
                            SlotIndex));
    }

    // 📝 The immediate extent is reset per use and therefore declares the resettable arrangement its slots do
    //    not. It is bring-up's staging path — a handful of transfers, each awaited — and never a rotation's.
    ExtentDeclaration.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    if (vkCreateCommandPool(Active, &ExtentDeclaration, nullptr, &ImmediateExtent) != VK_SUCCESS)
    {
        Reclaim();
        return Deliver<bool>::Refuse(
            { RefusalReason::ExtentExhausted, "the device rejected the immediate recording extent" });
    }

    // 📝 🔴 `06` §7's gate, by the two-operand form: there is one immediate extent and an ordinal on a single
    //    object reads as one of a set.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_COMMAND_POOL,
                        reinterpret_cast<std::uint64_t>(ImmediateExtent),
                        "CommandSequence immediate extent"));

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE OPENING
//------------------------------------------------------------------------------------------------------------------------

Deliver<VkCommandBuffer> CommandSequence::Open(std::uint32_t SlotIndex)
{
    if (DeviceEdge == nullptr || static_cast<std::size_t>(SlotIndex) >= Slots.size())
    {
        return Deliver<VkCommandBuffer>::Refuse(
            { RefusalReason::ContentUnsupported, "the cycle slot is outside the depth" });
    }

    RecordingSlot& Slot = Slots[SlotIndex];

    if (Slot.SlotOpen)
        return Deliver<VkCommandBuffer>::Refuse({ RefusalReason::RelationCyclic, "the slot is already open" });

    const VkDevice Active = DeviceEdge->ActiveDevice();

    // 🔴 The extent is reset whole rather than the recording individually. This is the point the previous
    //    rotation's commands are forgotten, and it is admissible only because `CycleScheduler::Await` has
    //    already established that no execution still reads them.
    if (vkResetCommandPool(Active, Slot.RecordingExtent, 0u) != VK_SUCCESS)
    {
        return Deliver<VkCommandBuffer>::Refuse(
            { RefusalReason::HostDenied, "the device failed to reset the slot's recording extent" });
    }

    VkCommandBufferBeginInfo OpenDeclaration = {};
    OpenDeclaration.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    OpenDeclaration.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(Slot.Primary, &OpenDeclaration) != VK_SUCCESS)
        return Deliver<VkCommandBuffer>::Refuse({ RefusalReason::HostDenied, "the device failed to open the recording" });

    Slot.SlotOpen = true;

    return Deliver<VkCommandBuffer>::Result(Slot.Primary);
}

Deliver<VkCommandBuffer> CommandSequence::Recording(std::uint32_t SlotIndex) const
{
    if (static_cast<std::size_t>(SlotIndex) >= Slots.size())
    {
        return Deliver<VkCommandBuffer>::Refuse(
            { RefusalReason::ContentUnsupported, "the cycle slot is outside the depth" });
    }

    if (!Slots[SlotIndex].SlotOpen)
        return Deliver<VkCommandBuffer>::Refuse({ RefusalReason::ContentUnsupported, "the slot is not open" });

    return Deliver<VkCommandBuffer>::Result(Slots[SlotIndex].Primary);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SURRENDER
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> CommandSequence::Submit(std::uint32_t SlotIndex, const SubmitOrdering& Ordering)
{
    if (DeviceEdge == nullptr || static_cast<std::size_t>(SlotIndex) >= Slots.size())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the cycle slot is outside the depth" });

    RecordingSlot& Slot = Slots[SlotIndex];

    if (!Slot.SlotOpen)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the slot is not open" });

    if (vkEndCommandBuffer(Slot.Primary) != VK_SUCCESS)
    {
        Slot.SlotOpen = false;
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the device failed to close the recording" });
    }

    VkSubmitInfo SubmitDeclaration       = {};
    SubmitDeclaration.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitDeclaration.commandBufferCount = 1u;
    SubmitDeclaration.pCommandBuffers    = &Slot.Primary;

    // 📝 Each ordering point is declared only when it is present. A null point counted as one is a vendor read
    //    of a handle that names nothing, which reports as a driver fault rather than as the omission it is.
    const VkPipelineStageFlags AwaitedStage = Ordering.AwaitedStage;

    if (Ordering.Awaited != VK_NULL_HANDLE)
    {
        SubmitDeclaration.waitSemaphoreCount = 1u;
        SubmitDeclaration.pWaitSemaphores    = &Ordering.Awaited;
        SubmitDeclaration.pWaitDstStageMask  = &AwaitedStage;
    }

    if (Ordering.Signalled != VK_NULL_HANDLE)
    {
        SubmitDeclaration.signalSemaphoreCount = 1u;
        SubmitDeclaration.pSignalSemaphores    = &Ordering.Signalled;
    }

    const VkResult Accepted = vkQueueSubmit(DeviceEdge->GraphicsQueue(), 1u, &SubmitDeclaration, Ordering.Completion);

    Slot.SlotOpen = false;

    // 🔴 `06` §7: reported upward, and the slot is closed either way. Nothing is destroyed here — the recording
    //    and its extent stay standing so that `06` §4.2's recovery reclaims them in its own order.
    if (Accepted == VK_ERROR_DEVICE_LOST)
        return Deliver<bool>::Refuse({ RefusalReason::DeviceLost, "the device was lost surrendering a cycle slot" });

    if (Accepted != VK_SUCCESS)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the queue rejected the surrender" });

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 OUTSIDE THE ROTATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<VkCommandBuffer> CommandSequence::OpenImmediate()
{
    if (DeviceEdge == nullptr || ImmediateExtent == VK_NULL_HANDLE)
        return Deliver<VkCommandBuffer>::Refuse({ RefusalReason::CapabilityAbsent, "no immediate extent stands" });

    VkCommandBufferAllocateInfo RecordingDeclaration = {};
    RecordingDeclaration.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    RecordingDeclaration.commandPool                 = ImmediateExtent;
    RecordingDeclaration.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    RecordingDeclaration.commandBufferCount          = 1u;

    VkCommandBuffer Incoming = VK_NULL_HANDLE;

    if (vkAllocateCommandBuffers(DeviceEdge->ActiveDevice(), &RecordingDeclaration, &Incoming) != VK_SUCCESS)
    {
        return Deliver<VkCommandBuffer>::Refuse(
            { RefusalReason::ExtentExhausted, "the device rejected an immediate recording" });
    }

    VkCommandBufferBeginInfo OpenDeclaration = {};
    OpenDeclaration.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    OpenDeclaration.flags                    = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(Incoming, &OpenDeclaration) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(DeviceEdge->ActiveDevice(), ImmediateExtent, 1u, &Incoming);
        return Deliver<VkCommandBuffer>::Refuse(
            { RefusalReason::HostDenied, "the device failed to open the immediate recording" });
    }

    // 📝 🔴 `06` §7's gate reaches the immediate recording too, and carries no ordinal because this path holds
    //    one at a time — `SubmitImmediate` waits for it and returns it before bring-up asks for the next.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_COMMAND_BUFFER,
                        reinterpret_cast<std::uint64_t>(Incoming),
                        "CommandSequence immediate recording"));

    return Deliver<VkCommandBuffer>::Result(Incoming);
}

Deliver<bool> CommandSequence::SubmitImmediate(VkCommandBuffer Recorded)
{
    if (DeviceEdge == nullptr || ImmediateExtent == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no immediate extent stands" });

    if (Recorded == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no recording to surrender" });

    const VkDevice Active = DeviceEdge->ActiveDevice();

    if (vkEndCommandBuffer(Recorded) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(Active, ImmediateExtent, 1u, &Recorded);
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the device failed to close the immediate recording" });
    }

    // 📝 A completion of its own rather than the rotation's. Awaiting the rotation's here would make bring-up's
    //    staging wait on a rotation that has not been recorded, and the two would then share an ordering point
    //    whose signalled meaning differs between them.
    VkFenceCreateInfo CompletionDeclaration = {};
    CompletionDeclaration.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence Completion = VK_NULL_HANDLE;

    if (vkCreateFence(Active, &CompletionDeclaration, nullptr, &Completion) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(Active, ImmediateExtent, 1u, &Recorded);
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the device rejected an ordering point" });
    }

    // 📝 🔴 `06` §7's gate. Named although it is destroyed a few lines below, because the report that matters
    //    here is the one raised **while** it stands — a device lost inside the wait names this point, and an
    //    unnamed one makes that report the only place in the ordering the reader cannot attribute.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_FENCE,
                        reinterpret_cast<std::uint64_t>(Completion),
                        "CommandSequence immediate completion"));

    VkSubmitInfo SubmitDeclaration       = {};
    SubmitDeclaration.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitDeclaration.commandBufferCount = 1u;
    SubmitDeclaration.pCommandBuffers    = &Recorded;

    const VkResult Accepted = vkQueueSubmit(DeviceEdge->GraphicsQueue(), 1u, &SubmitDeclaration, Completion);

    VkResult Reached = Accepted;

    if (Accepted == VK_SUCCESS)
        Reached = vkWaitForFences(Active, 1u, &Completion, VK_TRUE, CompletionLimitNanoseconds);

    vkDestroyFence(Active, Completion, nullptr);
    vkFreeCommandBuffers(Active, ImmediateExtent, 1u, &Recorded);

    // 📝 The recording and its completion are returned above regardless, because both were constructed here and
    //    a lost device leaves them unusable rather than owned by something else. What the loss reports is the
    //    device, which this never held.
    if (Accepted == VK_ERROR_DEVICE_LOST || Reached == VK_ERROR_DEVICE_LOST)
        return Deliver<bool>::Refuse({ RefusalReason::DeviceLost, "the device was lost during an immediate surrender" });

    if (Accepted != VK_SUCCESS)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the queue rejected the immediate surrender" });

    if (Reached == VK_TIMEOUT)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the immediate recording did not complete within the ceiling" });

    if (Reached != VK_SUCCESS)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the device rejected the wait" });

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void CommandSequence::Reclaim()
{
    if (DeviceEdge != nullptr && DeviceEdge->ActiveDevice() != VK_NULL_HANDLE)
    {
        const VkDevice Active = DeviceEdge->ActiveDevice();

        // 📝 The recordings are destroyed with their extent rather than freed first. Freeing them individually
        //    is bookkeeping the vendor discards a line later, and one freed twice is a fault at teardown.
        for (RecordingSlot& Slot : Slots)
        {
            if (Slot.RecordingExtent != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(Active, Slot.RecordingExtent, nullptr);
                Slot.RecordingExtent = VK_NULL_HANDLE;
            }

            Slot.Primary  = VK_NULL_HANDLE;
            Slot.SlotOpen = false;
        }

        if (ImmediateExtent != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(Active, ImmediateExtent, nullptr);
            ImmediateExtent = VK_NULL_HANDLE;
        }
    }

    Slots.clear();
}

CommandSequence::~CommandSequence()
{
    Reclaim();
}

}   // namespace Slate
