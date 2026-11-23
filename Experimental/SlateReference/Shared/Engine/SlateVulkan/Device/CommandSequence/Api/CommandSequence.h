//============================================================================================================================================
//                                                            COMMANDSEQUENCE.H
//============================================================================================================================================
// 🧩 One recording per cycle slot — where commands are written, and the ordered surrender of them to the queue.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateVulkan/Device/CycleScheduler/Api/CycleScheduler.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE ORDERING POINTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one surrender to the queue waits on and what it signals.
/// note  🔴 The awaited stage is declared alongside the awaited point rather than fixed here. A recording
///       that waits at the top of the ordering serialises against a point it only needs before it writes
///       colour, and the display stall that produces reads as a device too slow for the extent.
/// tag   nonallocating, nonthrowing
struct SubmitOrdering
{
    VkSemaphore           Awaited      = VK_NULL_HANDLE;                       // [-] - waited on before executing
    VkPipelineStageFlags  AwaitedStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;    // [-] - where the wait applies
    VkSemaphore           Signalled    = VK_NULL_HANDLE;                       // [-] - signalled at completion
    VkFence               Completion   = VK_NULL_HANDLE;                       // [-] - the host's ordering point
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RECORDINGS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The rotation-deep recordings every contributing document writes its commands into.
/// note  🔴 `06` §2.1 settles one graphics queue, so ordering between recordings is their order of surrender
///       rather than a queue arbitration. `08` §3's diagram is therefore the submission order verbatim, and
///       nothing here reorders what `RenderSchedule::Ordered` fixed.
/// note  ⚠️ One primary recording per cycle slot, reset whole. Resetting an individual recording costs the
///       vendor a per-recording allocator it must then keep, and `06` §7 sizes every per-recording resource
///       against the depth precisely so the whole slot can be reset at once.
/// tag   owning
class CommandSequence
{
public:

    CommandSequence()                                  = default;
    CommandSequence(const CommandSequence&)            = delete;
    CommandSequence& operator=(const CommandSequence&) = delete;
    ~CommandSequence();

    /// 🧩 Constructs the per-slot recording extents and the one primary recording each holds.
    /// in    Exchange  [-]  the created device; borrowed and outlives this component
    /// in    Naming    [-]  names every extent and every recording; borrowed and outlives this component
    /// out   Result   [-]  refuses with CapabilityAbsent when no device is active, ExtentExhausted when the
    ///                      device declines an extent or a recording; rejected in full
    /// post  `RecordingSlotCount` recordings stand, none of them open
    /// note  🔴 `06` §7's diagnostic-name gate. Each recording is named by its cycle slot, which is what the
    ///        driver's text needs to say — a report against an unnamed recording cannot distinguish the slot
    ///        being written from the one the device is still executing, and that pair is the whole rotation.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> ConstructCommandSequence(const VulkanExchange& Exchange, const DiagnosticExtension& Naming);

    /// 🧩 Resets one cycle slot's recording extent and opens its recording for writing.
    /// in    SlotIndex  [-]  below `RecordingSlotCount`
    /// out   Result       [-]  the opened recording; refuses with ContentUnsupported for an excessive slot
    ///                          and HostDenied when the device declines the reset or the open
    /// pre   🔴 `CycleScheduler::Await` delivered for this slot — the device no longer reads it
    /// post  the slot is open; Submit closes it
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<VkCommandBuffer> Open(std::uint32_t SlotIndex);

    /// 🧩 The recording one cycle slot holds, for a document contributing commands to an open slot.
    /// out   Result  [-]  refuses with ContentUnsupported for an excessive slot or a slot that is not open
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<VkCommandBuffer> Recording(std::uint32_t SlotIndex) const;

    /// 🧩 Closes one cycle slot's recording and surrenders it to the one graphics queue.
    /// in    SlotIndex [-]  below `RecordingSlotCount`
    /// in    Ordering     [-]  what the surrender waits on and signals; any member may be null
    /// out   Result      [-]  refuses with ContentUnsupported for a slot that is not open, HostDenied when
    ///                         the device declines the close or the surrender, and DeviceLost when the device
    ///                         was lost; the slot is closed and nothing is destroyed either way
    /// post  the slot is closed and executing; the completion is signalled when it finishes
    /// note  🔴 The completion is cleared by `CycleScheduler::Arm` immediately before this call and never here
    ///        — a component clearing an ordering point it does not own is one that clears it at the wrong
    ///        moment for every other reader of it.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Submit(std::uint32_t SlotIndex, const SubmitOrdering& Ordering);

    /// 🧩 Opens a recording outside the rotation, for the one-off transfers bring-up records.
    /// out   Result  [-]  refuses with ExtentExhausted when the device declines the recording
    /// note  ⚠️ Submitted and awaited immediately by `SubmitImmediate`. This is bring-up's path and never
    ///        a rotation's — an immediate wait inside a rotation is the whole device serialised on the host.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<VkCommandBuffer> OpenImmediate();

    /// 🧩 Closes an immediate recording, surrenders it, waits for it, and returns it.
    /// in    Recorded [-]  a recording OpenImmediate delivered
    /// out   Result  [-]  refuses with HostDenied when the device declines or does not complete, and with
    ///                     DeviceLost when the device was lost; the recording is returned either way
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> SubmitImmediate(VkCommandBuffer Recorded);

    /// 🧩 Destroys every recording and every extent.
    /// pre   the device is idle
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

private:

    struct RecordingSlot
    {
        VkCommandPool    RecordingExtent = VK_NULL_HANDLE;   // [-] - vendor spelling; reset whole
        VkCommandBuffer  Primary         = VK_NULL_HANDLE;   // [-] - the one recording of the slot
        bool             SlotOpen        = false;            // [-] - true between Open and Submit
    };

    // 📝 The same ceiling `CycleScheduler` waits under, for the same reason — a bounded wait reports a lost
    //    device where an indefinite one merely stops.
    static constexpr std::uint64_t CompletionLimitNanoseconds = 2000000000ull;   // [ns]

    const VulkanExchange*       DeviceEdge      = nullptr;         // [-] - borrowed; never owned
    const DiagnosticExtension*  NamingEdge      = nullptr;         // [-] - borrowed; never owned
    std::vector<RecordingSlot>  Slots           = {};              // [-] - RecordingSlotCount entries
    VkCommandPool               ImmediateExtent = VK_NULL_HANDLE;  // [-] - bring-up transfers, outside the rotation
};

}   // namespace Slate
