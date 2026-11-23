//============================================================================================================================================
//                                                          OCCLUSIONSCHEDULER.CPP
//============================================================================================================================================
// 🧩 The claimed chain, the per-level reduction it is filled by, and the two culling dispatches that compact survivors out of it.

#include "SlateCompute/Compute/VisibilityIndex/Api/OcclusionScheduler.h"

#include <cstring>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT THE DEVICE READS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The reduction's workgroup edge, matching `Shader/DepthReduction.slang`'s `[numthreads(8, 8, 1)]`. One invocation per texel
//    of the level being written, so the dispatch is the written extent rounded up to this on both ordinates.
constexpr std::uint32_t ReductionWorkgroupEdge = 8u;   // [-] - invocations per edge of one reduction workgroup

// 📝 The cull's flat workgroup extent, matching `Shader/OcclusionCulling.slang`'s `[numthreads(64, 1, 1)]`. One invocation per
//    **partition**, not per triangle — the compaction writes a run per surviving partition and the run length is the
//    partition's own triangle count, so a per-triangle lane would write the same run a hundred times.
constexpr std::uint32_t OcclusionWorkgroupLanes = 64u;   // [-] - invocations per cull workgroup

// 📝 Three ordinals per level in the extent span — where the level begins in the chain, and how far it runs on each axis. The
//    device selects its own level from a projected extent and then needs all three to address it, which is why the offset is
//    carried rather than re-derived: the halving rounds up, so a device-side prefix sum would have to repeat that rounding
//    exactly and a single disagreement addresses another level entirely.
constexpr std::uint32_t IndexsPerLevel = 3u;   // [-] - offset, along, across

// 📝 🔴 `VkDrawIndirectCommand` and not the indexed form. `16` §4's raster draws its fan with `vkCmdDraw` — the corners are
//    reached by division and remainder over the vertex ordinal and no index span exists — so an indexed record would name an
//    index buffer the draw does not bind. The vertex count is what the cull's atomic advances.
constexpr VkDeviceSize IndirectRecordBytes = sizeof(VkDrawIndirectCommand);   // [B] - one record per residency per slot

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OcclusionScheduler::ConstructOcclusionScheduler(SpanSpace&         Spans,
                                            ImageSpace&        Images,
                                            const TargetSpace& Reserved,
                                            ShaderCodec&       Modules,
                                            DescriptorIndex&   Descriptors,
                                            ProgramIndex&      Programs)
{
    SpanEdge       = &Spans;
    ImageEdge      = &Images;
    TargetEdge     = &Reserved;
    ModuleEdge     = &Modules;
    DescriptorEdge = &Descriptors;
    ProgramEdge    = &Programs;

    // 🔴 The three reduction slots and their order are the shader's, and the depth target is a **sampled** image rather than a
    //    storage one. `08` §2 claims it with `ImageIntent::DepthTarget`, whose usage accepts sampling and attachment and not
    //    storage; a layout declaring a storage image here is one the vendor accepts and the descriptor write then refuses.
    std::vector<DescriptorSlot> Reducing;

    DescriptorSlot ReductionRecord;
    ReductionRecord.SlotIndex    = 0u;
    ReductionRecord.Carried        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ReductionRecord.CarriedCount   = 1u;
    ReductionRecord.ReachingStages = VK_SHADER_STAGE_COMPUTE_BIT;

    DescriptorSlot DepthRead;
    DepthRead.SlotIndex    = 1u;
    DepthRead.Carried        = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    DepthRead.CarriedCount   = 1u;
    DepthRead.ReachingStages = VK_SHADER_STAGE_COMPUTE_BIT;

    DescriptorSlot ChainWritten;
    ChainWritten.SlotIndex    = 2u;
    ChainWritten.Carried        = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ChainWritten.CarriedCount   = 1u;
    ChainWritten.ReachingStages = VK_SHADER_STAGE_COMPUTE_BIT;

    Reducing.push_back(ReductionRecord);
    Reducing.push_back(DepthRead);
    Reducing.push_back(ChainWritten);

    const Deliver<std::uint32_t> ReductionDeclared = DescriptorEdge->Declare(Reducing);

    if (!ReductionDeclared.Resolved)
        return Deliver<bool>::Refuse(ReductionDeclared.Error);

    ReductionLayout = ReductionDeclared.Resolve();

    // 📝 Seven slots: record, chain, level extents, classification, surviving run, indirect record, verdicts.
    std::vector<DescriptorSlot> Culling;

    for (std::uint32_t SlotIndex = 0u; SlotIndex < 7u; ++SlotIndex)
    {
        DescriptorSlot Declaring;
        Declaring.SlotIndex    = SlotIndex;
        Declaring.Carried        = SlotIndex == 0u ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                                     : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        Declaring.CarriedCount   = 1u;
        Declaring.ReachingStages = VK_SHADER_STAGE_COMPUTE_BIT;

        Culling.push_back(Declaring);
    }

    const Deliver<std::uint32_t> OcclusionDeclared = DescriptorEdge->Declare(Culling);

    if (!OcclusionDeclared.Resolved)
        return Deliver<bool>::Refuse(OcclusionDeclared.Error);

    OcclusionLayout = OcclusionDeclared.Resolve();

    const Deliver<std::uint32_t> ReductionStream = ModuleEdge->Resolve("SlateCompute", "DepthReduction");

    if (!ReductionStream.Resolved)
        return Deliver<bool>::Refuse(ReductionStream.Error);

    const Deliver<std::uint32_t> OcclusionStream = ModuleEdge->Resolve("SlateCompute", "OcclusionCulling");

    if (!OcclusionStream.Resolved)
        return Deliver<bool>::Refuse(OcclusionStream.Error);

    ReductionModule  = ReductionStream.Resolve();
    OcclusionModule  = OcclusionStream.Resolve();

    ComputeDeclaration Reducer;
    Reducer.ModuleIndex  = ReductionModule;
    Reducer.LayoutIndexs = { ReductionLayout };

    const Deliver<std::uint32_t> ReducerProgram = ProgramEdge->DeclareCompute(Reducer);

    if (!ReducerProgram.Resolved)
        return Deliver<bool>::Refuse(ReducerProgram.Error);

    ComputeDeclaration Culler;
    Culler.ModuleIndex  = OcclusionModule;
    Culler.LayoutIndexs = { OcclusionLayout };

    const Deliver<std::uint32_t> CullerProgram = ProgramEdge->DeclareCompute(Culler);

    if (!CullerProgram.Resolved)
        return Deliver<bool>::Refuse(CullerProgram.Error);

    ReductionProgram = ReducerProgram.Resolve();
    OcclusionProgram = CullerProgram.Resolve();

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE ORDERING
//------------------------------------------------------------------------------------------------------------------------

void OcclusionScheduler::Order(VkCommandBuffer      Recorded,
                               VkPipelineStageFlags ReadStages,
                               VkAccessFlags        ReadAccess)
{
    // 📝 A global memory barrier rather than one per span. Every ordering this component records is "everything the dispatch
    //    just wrote, before everything the next thing reads", and naming the spans individually would be four buffer barriers
    //    saying the same thing — with four more places for a span added later to be forgotten.
    VkMemoryBarrier Ordered = {};
    Ordered.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    Ordered.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    Ordered.dstAccessMask = ReadAccess;

    vkCmdPipelineBarrier(Recorded, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, ReadStages,
                         0u, 1u, &Ordered, 0u, nullptr, 0u, nullptr);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CHAIN
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OcclusionScheduler::Derive(std::uint32_t DisplayX, std::uint32_t DisplayY)
{
    if (SpanEdge == nullptr || DescriptorEdge == nullptr || TargetEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "nothing was constructed" });

    const Deliver<bool> Derived = Chain.ConstructDepthReduction(DisplayX, DisplayY);

    if (!Derived.Resolved)
        return Derived;

    // 📝 The previous chain's spans are released before the new ones are claimed. `06` §7 requires the device to be idle here,
    //    so nothing is still reading them — and holding them would leak one chain's worth of extent per resize.
    if (ChainSpan != AbsentSpan)
        SpanEdge->Release(ChainSpan);

    if (LevelExtentSpan != AbsentSpan)
        SpanEdge->Release(LevelExtentSpan);

    for (const std::uint32_t Held : ReductionSpans)
    {
        if (Held != AbsentSpan)
            SpanEdge->Release(Held);
    }

    ChainSpan        = AbsentSpan;
    LevelExtentSpan  = AbsentSpan;
    ChainEverReduced = false;

    for (std::size_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
        ReducedFor[SlotIndex] = false;

    ReductionSpans.clear();
    LevelOffsets.clear();

    const std::uint32_t Levels = Chain.LevelCount();

    // 🔴 A span of reals and not of the depth target's own reduction levels. `DepthReduction` halves by rounding **up** and the
    //    vendor's level extents halve by rounding down, so the two disagree from the first odd coordinate; `08` §2 claims every
    //    target with one level; and the depth format accepts no storage usage, so no dispatch could write into it.
    SpanShape ChainShape;
    ChainShape.SpanBytes = Chain.ChainTexels() * static_cast<VkDeviceSize>(sizeof(float));
    ChainShape.Intent    = SpanIntent::StorageRead;
    ChainShape.Residency = ExtentResidency::DeviceLocal;

    const Deliver<SpanReservation> ChainReserved = SpanEdge->Reserve(ChainShape);

    if (!ChainReserved.Resolved)
        return Deliver<bool>::Refuse(ChainReserved.Error);

    ChainSpan = ChainReserved.Resolve().SpanIndex;

    // 📐 The offsets are accumulated here in the one place, from the same level extents the chain was sized against. Deriving
    //    them a second time on the device would be deriving one prefix sum twice.
    LevelOffsets.assign(static_cast<std::size_t>(Levels) * IndexsPerLevel, 0u);

    std::uint32_t Accumulated = 0u;

    for (std::uint32_t LevelIndex = 0u; LevelIndex < Levels; ++LevelIndex)
    {
        const Deliver<ReductionLevel> Held = Chain.Level(LevelIndex);

        if (!Held.Resolved)
            return Deliver<bool>::Refuse(Held.Error);

        LevelOffsets[LevelIndex * IndexsPerLevel]        = Accumulated;
        LevelOffsets[LevelIndex * IndexsPerLevel + 1u]   = Held.Resolve().Width;
        LevelOffsets[LevelIndex * IndexsPerLevel + 2u]   = Held.Resolve().Height;

        Accumulated += Held.Resolve().Width * Held.Resolve().Height;
    }

    // 📝 Host-writable rather than device-local, and deliberately so — the offsets are derived on the host and written once per
    //    derivation, so a device-local span would need a staging span and a recorded transfer to carry a few dozen words.
    SpanShape ExtentShape;
    ExtentShape.SpanBytes = static_cast<VkDeviceSize>(LevelOffsets.size() * sizeof(std::uint32_t));
    ExtentShape.Intent    = SpanIntent::StorageRead;
    ExtentShape.Residency = ExtentResidency::HostWritable;

    const Deliver<SpanReservation> ExtentReserved = SpanEdge->Reserve(ExtentShape);

    if (!ExtentReserved.Resolved)
        return Deliver<bool>::Refuse(ExtentReserved.Error);

    LevelExtentSpan = ExtentReserved.Resolve().SpanIndex;

    const Deliver<bool> ExtentWritten = SpanEdge->Amend(LevelExtentSpan,
                                                        LevelOffsets.data(),
                                                        ExtentShape.SpanBytes,
                                                        0u);

    if (!ExtentWritten.Resolved)
        return Deliver<bool>::Refuse(ExtentWritten.Error);

    // 📝 One uniform span per level per cycle slot. The record differs per level and the recording slot count is what keeps the
    //    slot being written from being the slot the device is reading, so both factors are real.
    ReductionSpans.assign(static_cast<std::size_t>(Levels) * RecordingSlotCount, AbsentSpan);

    for (std::size_t Index = 0u; Index < ReductionSpans.size(); ++Index)
    {
        SpanShape RecordShape;
        RecordShape.SpanBytes = static_cast<VkDeviceSize>(sizeof(UploadedReduction));
        RecordShape.Intent    = SpanIntent::UniformRead;
        RecordShape.Residency = ExtentResidency::HostWritable;

        const Deliver<SpanReservation> RecordReserved = SpanEdge->Reserve(RecordShape);

        if (!RecordReserved.Resolved)
            return Deliver<bool>::Refuse(RecordReserved.Error);

        ReductionSpans[Index] = RecordReserved.Resolve().SpanIndex;
    }

    if (ReductionReservations.empty())
    {
        for (std::uint32_t LevelIndex = 0u; LevelIndex < ReductionLevelLimit; ++LevelIndex)
        {
            const Deliver<std::uint32_t> Reserved = DescriptorEdge->Reserve(ReductionLayout);

            if (!Reserved.Resolved)
                return Deliver<bool>::Refuse(Reserved.Error);

            ReductionReservations.push_back(Reserved.Resolve());
        }
    }

    const Deliver<ImageReservation> DepthCurrent = TargetEdge->Resolve(SharedTarget::DepthSurface);

    if (!DepthCurrent.Resolved)
        return Deliver<bool>::Refuse(DepthCurrent.Error);

    const Deliver<SpanReservation> ChainCurrent  = SpanEdge->Current(ChainSpan);
    const Deliver<SpanReservation> ExtentCurrent = SpanEdge->Current(LevelExtentSpan);

    if (!ChainCurrent.Resolved || !ExtentCurrent.Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a claimed span no longer stands" });

    for (std::uint32_t LevelIndex = 0u; LevelIndex < Levels; ++LevelIndex)
    {
        for (std::uint32_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
        {
            const std::size_t SpanIndex = static_cast<std::size_t>(LevelIndex) * RecordingSlotCount + SlotIndex;

            const Deliver<SpanReservation> RecordCurrent = SpanEdge->Current(ReductionSpans[SpanIndex]);

            if (!RecordCurrent.Resolved)
                return Deliver<bool>::Refuse(RecordCurrent.Error);

            DescriptorContent Recording;
            Recording.SlotIndex = 0u;
            Recording.SpanExtent  = RecordCurrent.Resolve().Extent;
            Recording.SpanBytes   = RecordCurrent.Resolve().SpanBytes;

            DescriptorContent Depth;
            Depth.SlotIndex   = 1u;
            Depth.ImageView     = DepthCurrent.Resolve().WholeView;
            Depth.ImageCurrent = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            DescriptorContent Chained_;
            Chained_.SlotIndex = 2u;
            Chained_.SpanExtent  = ChainCurrent.Resolve().Extent;
            Chained_.SpanBytes   = ChainCurrent.Resolve().SpanBytes;

            const std::vector<DescriptorContent> Amending = { Recording, Depth, Chained_ };

            const Deliver<bool> Amended = DescriptorEdge->Amend(ReductionReservations[LevelIndex], SlotIndex, Amending);

            if (!Amended.Resolved)
                return Deliver<bool>::Refuse(Amended.Error);
        }
    }

    for (const CulledResidency& Current : Culled)
    {
        for (std::uint32_t PhaseIdx = 0u; PhaseIdx < static_cast<std::uint32_t>(CullingPhase::PhaseCount); ++PhaseIdx)
        {
            const CullingPhase Phase = static_cast<CullingPhase>(PhaseIdx);
            for (std::uint32_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
            {
                const std::uint32_t SlotIdx = PhaseSlot(Phase, SlotIndex);
                DescriptorContent Chained_;
                Chained_.SlotIndex = 1u;
                Chained_.SpanExtent  = ChainCurrent.Resolve().Extent;
                Chained_.SpanBytes   = ChainCurrent.Resolve().SpanBytes;

                DescriptorContent Extents;
                Extents.SlotIndex = 2u;
                Extents.SpanExtent  = ExtentCurrent.Resolve().Extent;
                Extents.SpanBytes   = ExtentCurrent.Resolve().SpanBytes;

                const std::vector<DescriptorContent> Amending = { Chained_, Extents };

                const Deliver<bool> Amended =
                    DescriptorEdge->Amend(Current.ReservationIndexs[SlotIdx], SlotIndex, Amending);

                if (!Amended.Resolved)
                    return Deliver<bool>::Refuse(Amended.Error);
            }
        }
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

void OcclusionScheduler::Abandon(CulledResidency& Abandoned)
{
    if (SpanEdge == nullptr)
        return;

    for (const std::uint32_t Held : Abandoned.ClassifiedSpans)
    {
        if (Held != AbsentSpan)
            SpanEdge->Release(Held);
    }

    for (const std::uint32_t Held : Abandoned.OcclusionSpans)
    {
        if (Held != AbsentSpan)
            SpanEdge->Release(Held);
    }

    for (const std::uint32_t Held : Abandoned.VerdictSpans)
    {
        if (Held != AbsentSpan)
            SpanEdge->Release(Held);
    }

    for (const std::uint32_t Held : Abandoned.RecordSpans)
    {
        if (Held != AbsentSpan)
            SpanEdge->Release(Held);
    }

    for (const std::uint32_t Held : Abandoned.SurvivingSpans)
    {
        if (Held != AbsentSpan)
            SpanEdge->Release(Held);
    }

    Abandoned = CulledResidency{};
}

Deliver<std::uint32_t> OcclusionScheduler::Resolve(std::uint32_t TriangleLimit, std::uint32_t PartitionCount)
{
    if (SpanEdge == nullptr || DescriptorEdge == nullptr)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::CapabilityAbsent, "nothing was constructed" });

    if (ChainSpan == AbsentSpan || LevelExtentSpan == AbsentSpan)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "no chain is derived" });

    if (TriangleLimit == 0u || PartitionCount == 0u)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "a residency carrying no partition and no triangle" });
    }

    CulledResidency Incoming;
    Incoming.TriangleLimit = TriangleLimit;
    Incoming.PartitionCount  = PartitionCount;

    const Deliver<SpanReservation> ChainCurrent  = SpanEdge->Current(ChainSpan);
    const Deliver<SpanReservation> ExtentCurrent = SpanEdge->Current(LevelExtentSpan);

    if (!ChainCurrent.Resolved || !ExtentCurrent.Resolved)
    {
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "a claimed span no longer stands" });
    }

    // 1. Per-slot spans: ClassifiedSpans, OcclusionSpans, VerdictSpans, AmendedFor
    for (std::uint32_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
    {
        SpanShape ClassifiedShape;
        ClassifiedShape.SpanBytes = static_cast<VkDeviceSize>(PartitionCount) * sizeof(ClassifiedPartition);
        ClassifiedShape.Intent    = SpanIntent::StorageRead;
        ClassifiedShape.Residency = ExtentResidency::HostWritable;

        const Deliver<SpanReservation> Classified = SpanEdge->Reserve(ClassifiedShape);

        if (!Classified.Resolved)
        {
            Abandon(Incoming);
            return Deliver<std::uint32_t>::Refuse(Classified.Error);
        }

        Incoming.ClassifiedSpans.push_back(Classified.Resolve().SpanIndex);

        SpanShape UniformShape;
        UniformShape.SpanBytes = static_cast<VkDeviceSize>(sizeof(UploadedOcclusion));
        UniformShape.Intent    = SpanIntent::UniformRead;
        UniformShape.Residency = ExtentResidency::HostWritable;

        const Deliver<SpanReservation> Uniform = SpanEdge->Reserve(UniformShape);

        if (!Uniform.Resolved)
        {
            Abandon(Incoming);
            return Deliver<std::uint32_t>::Refuse(Uniform.Error);
        }

        Incoming.OcclusionSpans.push_back(Uniform.Resolve().SpanIndex);

        SpanShape VerdictShape;
        VerdictShape.SpanBytes = static_cast<VkDeviceSize>(PartitionCount) * sizeof(std::uint32_t);
        VerdictShape.Intent    = SpanIntent::StorageRead;
        VerdictShape.Residency = ExtentResidency::DeviceLocal;

        const Deliver<SpanReservation> Verdict = SpanEdge->Reserve(VerdictShape);

        if (!Verdict.Resolved)
        {
            Abandon(Incoming);
            return Deliver<std::uint32_t>::Refuse(Verdict.Error);
        }

        Incoming.VerdictSpans.push_back(Verdict.Resolve().SpanIndex);
        Incoming.AmendedFor.push_back(false);
    }

    // 2. Per-phase-slot spans & descriptor sets: RecordSpans, SurvivingSpans, ReservationIndexs
    for (std::uint32_t PhaseIdx = 0u; PhaseIdx < static_cast<std::uint32_t>(CullingPhase::PhaseCount); ++PhaseIdx)
    {
        for (std::uint32_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
        {
            SpanShape SurvivingShape;
            SurvivingShape.SpanBytes = static_cast<VkDeviceSize>(TriangleLimit) * sizeof(std::uint32_t);
            SurvivingShape.Intent    = SpanIntent::StorageRead;
            SurvivingShape.Residency = ExtentResidency::DeviceLocal;

            const Deliver<SpanReservation> Surviving = SpanEdge->Reserve(SurvivingShape);

            if (!Surviving.Resolved)
            {
                Abandon(Incoming);
                return Deliver<std::uint32_t>::Refuse(Surviving.Error);
            }

            Incoming.SurvivingSpans.push_back(Surviving.Resolve().SpanIndex);

            SpanShape RecordShape;
            RecordShape.SpanBytes = IndirectRecordBytes;
            RecordShape.Intent    = SpanIntent::IndirectRecord;
            RecordShape.Residency = ExtentResidency::HostWritable;

            const Deliver<SpanReservation> Record = SpanEdge->Reserve(RecordShape);

            if (!Record.Resolved)
            {
                Abandon(Incoming);
                return Deliver<std::uint32_t>::Refuse(Record.Error);
            }

            Incoming.RecordSpans.push_back(Record.Resolve().SpanIndex);

            const Deliver<std::uint32_t> Reserved = DescriptorEdge->Reserve(OcclusionLayout);

            if (!Reserved.Resolved)
            {
                Abandon(Incoming);
                return Deliver<std::uint32_t>::Refuse(Reserved.Error);
            }

            Incoming.ReservationIndexs.push_back(Reserved.Resolve());
        }
    }

    // 3. Populate descriptor sets for all PhaseSlots
    for (std::uint32_t PhaseIdx = 0u; PhaseIdx < static_cast<std::uint32_t>(CullingPhase::PhaseCount); ++PhaseIdx)
    {
        const CullingPhase Phase = static_cast<CullingPhase>(PhaseIdx);
        for (std::uint32_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
        {
            const std::uint32_t SlotIdx = PhaseSlot(Phase, SlotIndex);

            const Deliver<SpanReservation> Uniform    = SpanEdge->Current(Incoming.OcclusionSpans[SlotIndex]);
            const Deliver<SpanReservation> Classified = SpanEdge->Current(Incoming.ClassifiedSpans[SlotIndex]);
            const Deliver<SpanReservation> Verdict    = SpanEdge->Current(Incoming.VerdictSpans[SlotIndex]);
            const Deliver<SpanReservation> Surviving  = SpanEdge->Current(Incoming.SurvivingSpans[SlotIdx]);
            const Deliver<SpanReservation> Record     = SpanEdge->Current(Incoming.RecordSpans[SlotIdx]);

            if (!Uniform.Resolved || !Classified.Resolved || !Verdict.Resolved ||
                !Surviving.Resolved || !Record.Resolved)
            {
                Abandon(Incoming);
                return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "a claimed span no longer stands" });
            }

            std::vector<DescriptorContent> Amending;

            DescriptorContent Recorded_;
            Recorded_.SlotIndex = 0u;
            Recorded_.SpanExtent  = Uniform.Resolve().Extent;
            Recorded_.SpanBytes   = Uniform.Resolve().SpanBytes;

            DescriptorContent Chained_;
            Chained_.SlotIndex = 1u;
            Chained_.SpanExtent  = ChainCurrent.Resolve().Extent;
            Chained_.SpanBytes   = ChainCurrent.Resolve().SpanBytes;

            DescriptorContent Extents;
            Extents.SlotIndex = 2u;
            Extents.SpanExtent  = ExtentCurrent.Resolve().Extent;
            Extents.SpanBytes   = ExtentCurrent.Resolve().SpanBytes;

            DescriptorContent Tested;
            Tested.SlotIndex = 3u;
            Tested.SpanExtent  = Classified.Resolve().Extent;
            Tested.SpanBytes   = Classified.Resolve().SpanBytes;

            DescriptorContent Survived;
            Survived.SlotIndex = 4u;
            Survived.SpanExtent  = Surviving.Resolve().Extent;
            Survived.SpanBytes   = Surviving.Resolve().SpanBytes;

            DescriptorContent Drawn;
            Drawn.SlotIndex = 5u;
            Drawn.SpanExtent  = Record.Resolve().Extent;
            Drawn.SpanBytes   = Record.Resolve().SpanBytes;

            DescriptorContent Verdicts_;
            Verdicts_.SlotIndex = 6u;
            Verdicts_.SpanExtent  = Verdict.Resolve().Extent;
            Verdicts_.SpanBytes   = Verdict.Resolve().SpanBytes;

            Amending.push_back(Recorded_);
            Amending.push_back(Chained_);
            Amending.push_back(Extents);
            Amending.push_back(Tested);
            Amending.push_back(Survived);
            Amending.push_back(Drawn);
            Amending.push_back(Verdicts_);

            const Deliver<bool> Amended = DescriptorEdge->Amend(Incoming.ReservationIndexs[SlotIdx], SlotIndex, Amending);

            if (!Amended.Resolved)
            {
                Abandon(Incoming);
                return Deliver<std::uint32_t>::Refuse(Amended.Error);
            }
        }
    }

    const std::uint32_t CullingIndex = static_cast<std::uint32_t>(Culled.size());

    Culled.push_back(Incoming);

    return Deliver<std::uint32_t>::Result(CullingIndex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OcclusionScheduler::Amend(std::uint32_t                           CullingIndex,
                                        std::uint32_t                           SlotIndex,
                                        const std::vector<ClassifiedPartition>& Classified)
{
    if (SpanEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "nothing was constructed" });

    if (CullingIndex >= static_cast<std::uint32_t>(Culled.size()))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no residency stands at that ordinal" });

    if (SlotIndex >= RecordingSlotCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the cycle slot is outside the depth" });

    CulledResidency& Current = Culled[CullingIndex];

    if (static_cast<std::uint32_t>(Classified.size()) != Current.PartitionCount)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the classification disagrees with the declared partition count" });
    }

    const Deliver<bool> Written = SpanEdge->Amend(Current.ClassifiedSpans[SlotIndex],
                                                  Classified.data(),
                                                  static_cast<VkDeviceSize>(Classified.size() * sizeof(ClassifiedPartition)),
                                                  0u);

    if (!Written.Resolved)
        return Deliver<bool>::Refuse(Written.Error);

    VkDrawIndirectCommand Cleared = {};
    Cleared.vertexCount   = 0u;
    Cleared.instanceCount = 1u;
    Cleared.firstVertex   = 0u;
    Cleared.firstInstance = 0u;

    for (std::uint32_t PhaseIdx = 0u; PhaseIdx < static_cast<std::uint32_t>(CullingPhase::PhaseCount); ++PhaseIdx)
    {
        const CullingPhase Phase = static_cast<CullingPhase>(PhaseIdx);
        const std::uint32_t SlotIdx = PhaseSlot(Phase, SlotIndex);

        const Deliver<bool> Recorded = SpanEdge->Amend(Current.RecordSpans[SlotIdx],
                                                       &Cleared,
                                                       IndirectRecordBytes,
                                                       0u);

        if (!Recorded.Resolved)
            return Deliver<bool>::Refuse(Recorded.Error);
    }

    Current.AmendedFor[SlotIndex] = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REDUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OcclusionScheduler::ReduceLevel(VkCommandBuffer Recorded,
                                              std::uint32_t   SlotIndex,
                                              std::uint32_t   LevelIndex)
{
    const Deliver<ReductionLevel> Written = Chain.Level(LevelIndex);

    if (!Written.Resolved)
        return Deliver<bool>::Refuse(Written.Error);

    UploadedReduction Reducing;
    Reducing.WrittenLevel  = LevelIndex;
    Reducing.WrittenOffset = LevelOffsets[LevelIndex * IndexsPerLevel];
    Reducing.WrittenX  = Written.Resolve().Width;
    Reducing.WrittenY = Written.Resolve().Height;

    if (LevelIndex == 0u)
    {
        Reducing.SourceOffset     = 0u;
        Reducing.SourceX      = Written.Resolve().Width;
        Reducing.SourceY     = Written.Resolve().Height;
        Reducing.SourceFromTarget = 1u;
    }
    else
    {
        const Deliver<ReductionLevel> Source = Chain.Level(LevelIndex - 1u);

        if (!Source.Resolved)
            return Deliver<bool>::Refuse(Source.Error);

        Reducing.SourceOffset     = LevelOffsets[(LevelIndex - 1u) * IndexsPerLevel];
        Reducing.SourceX      = Source.Resolve().Width;
        Reducing.SourceY     = Source.Resolve().Height;
        Reducing.SourceFromTarget = 0u;
    }

    const std::size_t SpanIndex = static_cast<std::size_t>(LevelIndex) * RecordingSlotCount + SlotIndex;

    const Deliver<bool> Amended = SpanEdge->Amend(ReductionSpans[SpanIndex],
                                                  &Reducing,
                                                  static_cast<VkDeviceSize>(sizeof(Reducing)),
                                                  0u);

    if (!Amended.Resolved)
        return Deliver<bool>::Refuse(Amended.Error);

    const Deliver<ConstructedProgram> Program = ProgramEdge->Resolve(ReductionProgram);

    if (!Program.Resolved)
        return Deliver<bool>::Refuse(Program.Error);

    const Deliver<VkDescriptorSet> Reaching =
        DescriptorEdge->Resolve(ReductionReservations[LevelIndex], SlotIndex);

    if (!Reaching.Resolved)
        return Deliver<bool>::Refuse(Reaching.Error);

    const ConstructedProgram& Constructed = Program.Resolve();
    const VkDescriptorSet     Reached     = Reaching.Resolve();

    vkCmdBindPipeline(Recorded, Constructed.RecordedAs, Constructed.Constructed);
    vkCmdBindDescriptorSets(Recorded, Constructed.RecordedAs, Constructed.ReachedLayout, 0u, 1u, &Reached, 0u, nullptr);

    const std::uint32_t GroupsX  = (Written.Resolve().Width  + ReductionWorkgroupEdge - 1u) / ReductionWorkgroupEdge;
    const std::uint32_t GroupsY = (Written.Resolve().Height + ReductionWorkgroupEdge - 1u) / ReductionWorkgroupEdge;

    vkCmdDispatch(Recorded, GroupsX, GroupsY, 1u);

    return Deliver<bool>::Result(true);
}

Deliver<bool> OcclusionScheduler::Reduce(VkCommandBuffer Recorded, std::uint32_t SlotIndex)
{
    if (SpanEdge == nullptr || ProgramEdge == nullptr || ImageEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "nothing was constructed" });

    if (Recorded == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no recording was supplied" });

    if (SlotIndex >= RecordingSlotCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the cycle slot is outside the depth" });

    if (ChainSpan == AbsentSpan || ReductionReservations.empty())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no chain is derived" });

    const Deliver<std::uint32_t> DepthIndex = TargetEdge->IndexOf(SharedTarget::DepthSurface);

    if (!DepthIndex.Resolved)
        return Deliver<bool>::Refuse(DepthIndex.Error);

    const Deliver<bool> Transitioned =
        ImageEdge->Transition(Recorded, DepthIndex.Resolve(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!Transitioned.Resolved)
        return Deliver<bool>::Refuse(Transitioned.Error);

    const std::uint32_t Levels = Chain.LevelCount();

    for (std::uint32_t LevelIndex = 0u; LevelIndex < Levels; ++LevelIndex)
    {
        if (LevelIndex != 0u)
            Order(Recorded, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

        const Deliver<bool> Written = ReduceLevel(Recorded, SlotIndex, LevelIndex);

        if (!Written.Resolved)
            return Written;
    }

    Order(Recorded, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    ReducedFor[SlotIndex] = true;
    ChainEverReduced         = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE CULL
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> OcclusionScheduler::Cull(VkCommandBuffer Recorded, std::uint32_t SlotIndex, CullingPhase Phase)
{
    if (SpanEdge == nullptr || ProgramEdge == nullptr || DescriptorEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "nothing was constructed" });

    if (Recorded == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no recording was supplied" });

    if (SlotIndex >= RecordingSlotCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the cycle slot is outside the depth" });

    if (Phase == CullingPhase::PhaseCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such culling phase" });

    if (ChainSpan == AbsentSpan)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no chain is derived" });

    if (Phase == CullingPhase::AgainstCurrent && !ReducedFor[SlotIndex])
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "Reduce has not been recorded for this cycle slot" });
    }

    const Deliver<ConstructedProgram> Program = ProgramEdge->Resolve(OcclusionProgram);

    if (!Program.Resolved)
        return Deliver<bool>::Refuse(Program.Error);

    const ConstructedProgram& Constructed = Program.Resolve();

    vkCmdBindPipeline(Recorded, Constructed.RecordedAs, Constructed.Constructed);

    for (CulledResidency& Current : Culled)
    {
        if (Current.PartitionCount == 0u)
            continue;

        if (Phase == CullingPhase::AgainstPrevious && !Current.AmendedFor[SlotIndex])
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "Amend has not written this cycle slot since the last cull" });
        }

        UploadedOcclusion Testing;
        Testing.ClassifiedCount = Current.PartitionCount;
        Testing.DisplayX    = Chain.DisplayX();
        Testing.DisplayY   = Chain.DisplayY();
        Testing.TriangleLimit = Current.TriangleLimit;
        Testing.PhaseIndex    = static_cast<std::uint32_t>(Phase);

        Testing.LevelCount = (Phase == CullingPhase::AgainstPrevious && !ChainEverReduced) ? 0u : Chain.LevelCount();

        const Deliver<bool> Written = SpanEdge->Amend(Current.OcclusionSpans[SlotIndex],
                                                      &Testing,
                                                      static_cast<VkDeviceSize>(sizeof(Testing)),
                                                      0u);

        if (!Written.Resolved)
            return Deliver<bool>::Refuse(Written.Error);

        const std::uint32_t SlotIdx = PhaseSlot(Phase, SlotIndex);

        const Deliver<VkDescriptorSet> Reaching =
            DescriptorEdge->Resolve(Current.ReservationIndexs[SlotIdx], SlotIndex);

        if (!Reaching.Resolved)
            return Deliver<bool>::Refuse(Reaching.Error);

        const VkDescriptorSet Reached = Reaching.Resolve();

        vkCmdBindDescriptorSets(Recorded, Constructed.RecordedAs, Constructed.ReachedLayout,
                                0u, 1u, &Reached, 0u, nullptr);

        const std::uint32_t Groups =
            (Current.PartitionCount + OcclusionWorkgroupLanes - 1u) / OcclusionWorkgroupLanes;

        vkCmdDispatch(Recorded, Groups, 1u, 1u);

        if (Phase == CullingPhase::AgainstCurrent)
        {
            Current.AmendedFor[SlotIndex] = false;
        }
    }

    Order(Recorded,
          VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
          VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);

    if (Phase == CullingPhase::AgainstCurrent)
    {
        ReducedFor[SlotIndex] = false;
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE READS
//------------------------------------------------------------------------------------------------------------------------

Deliver<VkBuffer> OcclusionScheduler::RecordOf(std::uint32_t CullingIndex,
                                               std::uint32_t SlotIndex,
                                               CullingPhase  Phase) const
{
    if (SpanEdge == nullptr || CullingIndex >= static_cast<std::uint32_t>(Culled.size()))
        return Deliver<VkBuffer>::Refuse({ RefusalReason::ContentUnsupported, "no residency stands at that ordinal" });

    if (SlotIndex >= RecordingSlotCount || Phase == CullingPhase::PhaseCount)
        return Deliver<VkBuffer>::Refuse({ RefusalReason::ContentUnsupported, "the cycle slot or phase is outside range" });

    const std::uint32_t SlotIdx = PhaseSlot(Phase, SlotIndex);
    const Deliver<SpanReservation> Current = SpanEdge->Current(Culled[CullingIndex].RecordSpans[SlotIdx]);

    if (!Current.Resolved)
        return Deliver<VkBuffer>::Refuse(Current.Error);

    return Deliver<VkBuffer>::Result(Current.Resolve().Extent);
}

Deliver<VkBuffer> OcclusionScheduler::SurvivingOf(std::uint32_t CullingIndex,
                                                  std::uint32_t SlotIndex,
                                                  CullingPhase  Phase) const
{
    if (SpanEdge == nullptr || CullingIndex >= static_cast<std::uint32_t>(Culled.size()))
        return Deliver<VkBuffer>::Refuse({ RefusalReason::ContentUnsupported, "no residency stands at that ordinal" });

    if (SlotIndex >= RecordingSlotCount || Phase == CullingPhase::PhaseCount)
        return Deliver<VkBuffer>::Refuse({ RefusalReason::ContentUnsupported, "the cycle slot or phase is outside range" });

    const std::uint32_t SlotIdx = PhaseSlot(Phase, SlotIndex);
    const Deliver<SpanReservation> Current = SpanEdge->Current(Culled[CullingIndex].SurvivingSpans[SlotIdx]);

    if (!Current.Resolved)
        return Deliver<VkBuffer>::Refuse(Current.Error);

    return Deliver<VkBuffer>::Result(Current.Resolve().Extent);
}

std::uint32_t OcclusionScheduler::CulledCount() const   { return static_cast<std::uint32_t>(Culled.size()); }
std::uint32_t OcclusionScheduler::LevelCount() const    { return Chain.LevelCount();                        }
bool          OcclusionScheduler::ChainDerived() const  { return ChainSpan != AbsentSpan;                   }
bool          OcclusionScheduler::ChainReduced() const  { return ChainEverReduced;                          }

bool OcclusionScheduler::ProgramsCurrent() const
{
    return ReductionProgram != AbsentProgram && OcclusionProgram != AbsentProgram;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void OcclusionScheduler::Reclaim()
{
    if (SpanEdge == nullptr)
        return;

    for (CulledResidency& Current : Culled)
        Abandon(Current);

    Culled.clear();

    for (const std::uint32_t Held : ReductionSpans)
    {
        if (Held != AbsentSpan)
            SpanEdge->Release(Held);
    }

    ReductionSpans.clear();
    LevelOffsets.clear();

    if (ChainSpan != AbsentSpan)
        SpanEdge->Release(ChainSpan);

    if (LevelExtentSpan != AbsentSpan)
        SpanEdge->Release(LevelExtentSpan);

    ChainSpan        = AbsentSpan;
    LevelExtentSpan  = AbsentSpan;
    ChainEverReduced = false;

    Chain.Reclaim();

    for (std::size_t SlotIndex = 0u; SlotIndex < RecordingSlotCount; ++SlotIndex)
        ReducedFor[SlotIndex] = false;
}

}   // namespace Slate
