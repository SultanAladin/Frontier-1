//============================================================================================================================================
//                                                            OCCLUSIONSCHEDULER.H
//============================================================================================================================================
// 🧩 The device half of `16` §2 — the reduction chain, the two culling phases over it, and the indirect draw each one records.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateCompute/Compute/VisibilityIndex/Api/DepthReduction.h"
#include "SlateCompute/Compute/VisibilityIndex/Api/PartitionClassifier.h"
#include "SlateVulkan/Device/DescriptorIndex/Api/DescriptorIndex.h"
#include "SlateVulkan/Device/ImageSpace/Api/ImageSpace.h"
#include "SlateVulkan/Device/ProgramIndex/Api/ProgramIndex.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"
#include "SlateVulkan/Device/ShaderCodec/Api/ShaderCodec.h"
#include "SlateVulkan/Device/SpanSpace/Api/SpanSpace.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHICH PHASE IS RECORDED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which of `16` §2's two culling phases a dispatch is recording.
/// note  🔴 The two differ in **which reduction they read** and in nothing else. ① reads the reduction the
///        previous rotation left standing and ③ reads the one ② reduced from this rotation's depth; the
///        arithmetic, the program and the spans are the same. That is why one entry point serves both and why
///        this names the phase rather than declaring a second program for it.
/// note  🔴 Both are required. `16` §2 tests against last rotation's reduction to record a draw without waiting,
///        and then against this rotation's to admit what the camera has just moved into view — a single-phase
///        arrangement rejects every silhouette the camera approached, which reads as geometry appearing late.
/// tag   guarantee
enum class CullingPhase : std::uint32_t
{
    AgainstPrevious = 0u,   // [-] - ①; the reduction the previous rotation left standing
    AgainstCurrent  = 1u,   // [-] - ③; the reduction ② derived from this rotation's depth
    PhaseCount      = 2u    // [-] - the closed count, never a phase
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE UPLOADED RECORDS
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 The two records below are the host halves of what `Shader/OcclusionUniform.slang` declares, and they are
//    held in agreement by the assertions beneath them rather than by review. `ClassifiedPartition` is the third
//    and carries its own assertion in `PartitionClassifier.h`, because the classifier is what produces it.

/// 🧩 Which level of the chain one reduction dispatch writes, as the device reads it.
/// note  🔴 `SourceFromTarget` is declared and never derived from `WrittenLevel` being nought. The derivation
///        holds only while the finest level's extent is the display's, and that is a condition the record cannot
///        see — the level chain is derived against an extent the display has since been resized past.
/// tag   nonallocating, nonthrowing
struct UploadedReduction
{
    std::uint32_t  WrittenLevel     = 0u;   // [-]     - which level the dispatch writes
    std::uint32_t  WrittenOffset    = 0u;   // [texel] - where that level begins in the chain span
    std::uint32_t  WrittenX     = 0u;   // [texel] - its extent
    std::uint32_t  WrittenY    = 0u;   // [texel]
    std::uint32_t  SourceOffset     = 0u;   // [texel] - where the level above begins; unread when the target is read
    std::uint32_t  SourceX      = 0u;   // [texel] - the source's extent
    std::uint32_t  SourceY     = 0u;   // [texel]
    std::uint32_t  SourceFromTarget = 0u;   // [-]     - non-zero for the finest level
};

/// 🧩 What the occlusion dispatch reads of the chain and of the run it is testing.
/// tag   nonallocating, nonthrowing
struct UploadedOcclusion
{
    std::uint32_t  ClassifiedCount = 0u;   // [-]  - partitions the dispatch tests
    std::uint32_t  LevelCount      = 0u;   // [-]  - levels the chain carries
    std::uint32_t  DisplayX    = 0u;   // [px] - the extent the finest level covers
    std::uint32_t  DisplayY   = 0u;   // [px]
    std::uint32_t  TriangleLimit = 0u;   // [-]  - entries the surviving run can hold
    std::uint32_t  PhaseIndex    = 0u;   // [-]  - which of `16` §2's two phases is dispatching
    std::uint32_t  Unoccupied1     = 0u;   // [-]
    std::uint32_t  Unoccupied2     = 0u;   // [-]
};

// 📐 The widths the device reads, asserted rather than commented. A record that grew on one side alone is a block
//    the device walks at the wrong offset, and every member past the first then reads its predecessor.
static_assert(sizeof(UploadedReduction) == 32u, "the device reads eight 32-bit ordinals per reduction dispatch");
static_assert(sizeof(UploadedOcclusion) == 32u, "the device reads eight 32-bit ordinals per occlusion dispatch");

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT ONE RUN OCCUPIES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The spans one resident partitioning's culling occupies, claimed once and written per rotation.
/// note  🔴 One of these per residency and never one shared. The compaction writes triangle ordinals into the
///        residency's **own** fanned run, and a run laid across two residencies would draw the second
///        partitioning's triangles against the first's positions and the first's placement.
/// note  🔴 The classified span and the record are host-writable rather than device-local, and deliberately so.
///        The classification is produced on the host every rotation and the record is cleared to a corner count
///        of nought every rotation; a device-local pair would need two staging spans and two transfers to carry
///        the same twelve bytes a coherent write places directly.
/// note  ⚠️ The surviving run is sized to the residency's **whole** triangle count. A tighter claim would be a
///        bound on how much can survive, and nothing about the cull supplies one — every partition surviving is
///        the ordinary case for a camera looking at an unoccluded object.
/// note  🔴 The record, the surviving run and the descriptor claim are **per phase as well as per rotation
///        slot**. Phase ④ dispatches while phase ①'s draw is already recorded against ①'s record, so a shared
///        record would have ④ append corners to a count ①'s draw is about to read — and the draw would issue
///        corners belonging to partitions it was told were occluded. Recorded as the second structural closure.
/// note  🔴 The verdict run is per cycle slot and **not** per phase, because it is the one thing the two
///        phases pass between them. Phase ① writes every partition's verdict and phase ④ reads it, so that ④
///        tests only what ① rejected on depth; without it ④ would re-admit every survivor and draw it twice.
/// tag   owning
struct CulledResidency
{
    std::vector<std::uint32_t>  ClassifiedSpans = {};           // [-] - one per cycle slot, host-writable
    std::vector<std::uint32_t>  OcclusionSpans  = {};           // [-] - the uniform, one per cycle slot
    std::vector<std::uint32_t>  VerdictSpans    = {};           // [-] - ①'s per-partition verdict, per slot
    std::vector<std::uint32_t>  RecordSpans     = {};           // [-] - PhaseSlot-indexed indirect records
    std::vector<std::uint32_t>  SurvivingSpans  = {};           // [-] - PhaseSlot-indexed compacted ordinals
    std::vector<std::uint32_t>  ReservationIndexs   = {};           // [-] - PhaseSlot-indexed culling claims
    std::vector<bool>           AmendedFor      = {};           // [-] - one per cycle slot
    std::uint32_t               TriangleLimit = 0u;           // [-] - triangles the residency's fan carries
    std::uint32_t               PartitionCount  = 0u;           // [-] - partitions it declared
};

/// 🧩 Where one phase's per-slot entry sits in a PhaseSlot-indexed run.
/// note  📝 Phase-major rather than slot-major, so a phase's whole run is contiguous and a reclamation walks it
///        without a stride. Nothing depends on the ordering beyond the two agreeing, which is why it is one
///        routine rather than a repeated product.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr std::uint32_t PhaseSlot(CullingPhase Phase, std::uint32_t SlotIndex)
{
    return static_cast<std::uint32_t>(Phase) * RecordingSlotCount + SlotIndex;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TWO-PHASE CULL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `16` §2's device half — the hierarchical minimum, the two culling phases against it, and the compaction
///     each one leaves for the indirect draw that follows.
/// note  🔴 The chain is a **span** and not the depth image's own reduction levels, for three reasons that agree.
///        `DepthReduction` halves by rounding up and the vendor's level extents halve by rounding down, so the
///        two disagree from the first odd coordinate; `08` §2 claims every target with one level; and the depth
///        format accepts no storage usage, so no dispatch could write into it. What is claimed here is therefore
///        `DepthReduction::ChainTexels()` reals, with the level offsets carried beside them.
/// note  🔴 The depth target is **read** by the reduction and never written. `16` §2 ② reduces what the raster
///        recorded, so the recording order within one rotation is raster, then reduction, then phase ③ — and the
///        transition into a sampled layout is `ImageSpace`'s, recorded here and registered nowhere else.
/// note  ⚠️ 🚧 The compute raster of `16` §3 is a separate route against the same targets. A partition that
///        projects to nothing survives this cull deliberately — it is sub-pixel and belongs to that route, and
///        rejecting it here would drop geometry no other route then draws.
/// tag   owning
class OcclusionScheduler
{
public:

    OcclusionScheduler()                                     = default;
    OcclusionScheduler(const OcclusionScheduler&)            = delete;
    OcclusionScheduler& operator=(const OcclusionScheduler&) = delete;

    /// 🧩 Declares both layouts, resolves both modules, and constructs the two compute programs.
    /// in    Spans        [-]  where every span is claimed; borrowed and outlives this component
    /// in    Images       [-]  where the depth target's view is resolved; borrowed and outlives this component
    /// in    Reserved      [-]  the shared target set, for the depth target's ordinal; borrowed
    /// in    Modules      [-]  where both lowered streams are resolved; borrowed, non-const for the declaration
    /// in    Descriptors  [-]  where both layouts are declared; borrowed and outlives this component
    /// in    Programs     [-]  where both programs are constructed; borrowed and outlives this component
    /// out   Result      [-]  refuses with whatever the layouts, the modules or the programs rejected
    /// pre   🔴 the descriptor declaration is not yet fixed — `Declare` refuses once it is
    /// post  both programs stand; no chain is derived and no residency is claimable
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructOcclusionScheduler(SpanSpace&        Spans,
                            ImageSpace&       Images,
                            const TargetSpace& Reserved,
                            ShaderCodec&      Modules,
                            DescriptorIndex&  Descriptors,
                            ProgramIndex&     Programs);

    /// 🧩 Derives the level chain against one display extent and claims the span that holds it.
    /// in    DisplayX   [px]  the extent this rotation is recorded against
    /// in    DisplayY  [px]
    /// out   Result        [-]   refuses with whatever `DepthReduction` or the claim rejected
    /// pre   🔴 the device is idle — every rotation reading the previous chain has completed
    /// post  the chain span stands and the level offsets are held beside it
    /// note  🔴 The chain is **not** cleared on derivation and does not need to be. Phase ① of the first rotation
    ///        after a derivation reads a chain nothing has reduced into, and what an unwritten span holds is not
    ///        declared — so the first rotation's phase ① is recorded as testing nothing rather than as testing
    ///        against arbitrary depth. `ChainReduced` below is what distinguishes them.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Derive(std::uint32_t DisplayX, std::uint32_t DisplayY);

    /// 🧩 Reservations the culling spans for one resident partitioning.
    /// in    TriangleLimit  [-]  triangles the residency's fan carries; the surviving run is sized to it
    /// in    PartitionCount   [-]  partitions the registration declared
    /// out   Result          [-]  the culling ordinal; refuses with whatever the claim rejected and with
    ///                             ContentUnsupported for a residency carrying no triangle
    /// pre   🔴 `DescriptorIndex::Fix` delivered, and `Derive` has claimed the chain
    /// post  the spans stand and are written every rotation until the topology changes
    /// note  🔴 The descriptor set is written **here**, once per cycle slot. Every span it names stands for
    ///        the life of the residency, so a per-slot write would rewrite one arrangement with itself — and
    ///        would do it to a set the previous rotation's dispatch is still reading.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Resolve(std::uint32_t TriangleLimit, std::uint32_t PartitionCount);

    /// 🧩 Writes one rotation's classification into a residency's span and clears its indirect record.
    /// in    CullingIndex  [-]  an ordinal this component registered
    /// in    SlotIndex    [-]  below `RecordingSlotCount`
    /// in    Classified      [-]  one entry per partition, as `ClassifyPartition` produced them in order
    /// out   Result         [-]  refuses with ContentUnsupported for an unclaimed ordinal or a run disagreeing
    ///                            with the declared partition count, and with whatever the write rejected
    /// pre   🔴 no recording that reads this cycle slot's spans is still executing
    /// note  🔴 **Both** phases' records are cleared to a corner count of nought here and never on the device.
    ///        The atomic only ever advances a count, so a record carrying the previous rotation's would have
    ///        this rotation's survivors appended to it — and the draw would issue corners nothing wrote.
    /// note  📝 Excluded partitions are carried across rather than dropped. The dispatch tests one partition per
    ///        invocation and reads the standing to skip what the host already rejected, which keeps the classified
    ///        run at the partition count and lets the entry point's dispatch extent be that same count.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Amend(std::uint32_t                             CullingIndex,
                        std::uint32_t                             SlotIndex,
                        const std::vector<ClassifiedPartition>&   Classified);

    /// 🧩 Records ② — one dispatch per level, reducing the depth target into the chain.
    /// in    Recorded      [-]  the open recording of this cycle slot
    /// in    SlotIndex  [-]  below `RecordingSlotCount`
    /// out   Result       [-]  refuses with ContentUnsupported before the chain is derived, and with whatever
    ///                          the descriptor write, the transition or the program resolution rejected
    /// post  ChainReduced holds; phase ③ may be recorded against it
    /// note  🔴 One barrier **between** every pair of levels and not one before the run. Each level reads the
    ///        level above it, so a run recorded without them has every dispatch reading whatever its predecessor
    ///        had written when the scheduling reached it — which is a reduction that is correct on one driver and
    ///        differs by a level of depth on the next.
    /// note  🔴 The depth target is transitioned into a shader-read layout here, through `ImageSpace` and nowhere
    ///        else. `08` §4 keeps every layout transition in that one place; a barrier registered at this site would
    ///        leave its record naming a layout the image is not in, and the next transition would then be registered
    ///        from the wrong one.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Reduce(VkCommandBuffer Recorded, std::uint32_t SlotIndex);

    /// 🧩 Records ① or ③ — one dispatch per residency, testing its partitions and compacting the survivors.
    /// in    Recorded      [-]  the open recording of this cycle slot
    /// in    SlotIndex  [-]  below `RecordingSlotCount`
    /// in    Phase         [-]  which reduction is being tested against
    /// out   Result       [-]  refuses with ContentUnsupported before the chain is derived, before `Amend` has
    ///                          written this cycle slot, and for `AgainstCurrent` before `Reduce` was recorded
    /// post  every residency's record names the corners its survivors amount to
    /// note  🔴 The barrier after the dispatch declares the record as an **indirect** read and the surviving run
    ///        as a vertex-stage storage read. A draw recorded without it reads the record at whatever moment the
    ///        scheduling reaches it, and the corner count it finds there is the count as of some earlier
    ///        invocation — which draws a prefix of the survivors and reads as geometry culled at random.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Cull(VkCommandBuffer Recorded, std::uint32_t SlotIndex, CullingPhase Phase);

    /// 🧩 The record and the surviving run one residency's draw is registered from.
    /// in    CullingIndex  [-]  an ordinal this component registered
    /// in    SlotIndex    [-]  below `RecordingSlotCount`
    /// in    Phase           [-]  which of the two phases the draw follows
    /// out   Result         [-]  the indirect record's vendor span; refuses with ContentUnsupported for an
    ///                            unclaimed ordinal or an excessive cycle slot
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<VkBuffer> RecordOf(std::uint32_t CullingIndex,
                               std::uint32_t SlotIndex,
                               CullingPhase  Phase) const;

    /// 🧩 The compacted triangle ordinals one residency's vertex stage resolves through.
    /// out   Result  [-]  refuses with ContentUnsupported for an unclaimed ordinal
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<VkBuffer> SurvivingOf(std::uint32_t CullingIndex,
                                  std::uint32_t SlotIndex,
                                  CullingPhase  Phase) const;

    /// 🧩 Releases every culling span and the chain.
    /// pre   the device is idle
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

    std::uint32_t  CulledCount() const;
    std::uint32_t  LevelCount() const;
    bool           ChainDerived() const;
    bool           ChainReduced() const;
    bool           ProgramsCurrent() const;

private:

    /// 🧩 Records the barrier that orders one storage write against the read that follows it.
    /// in    Recorded    [-]  the recording being written into
    /// in    ReadStages  [-]  which stages read what was just written, as the vendor spells them
    /// in    ReadAccess  [-]  what those stages read it as
    /// note  📝 Spelled here rather than reached for because `SpanSpace` holds no barrier at all — `Transfer`
    ///        declares the ordering as the caller's, since only the caller knows which stage reads the bytes.
    static void Order(VkCommandBuffer       Recorded,
                      VkPipelineStageFlags  ReadStages,
                      VkAccessFlags         ReadAccess);

    /// 🧩 Writes one level's reduction record and records the dispatch that reduces into it.
    /// in    Recorded      [-]  the recording being written into
    /// in    SlotIndex  [-]  below `RecordingSlotCount`
    /// in    LevelIndex  [-]  the level being written; nought reads the depth target
    /// out   Result       [-]  refuses with whatever the write or the resolution rejected
    Deliver<bool> ReduceLevel(VkCommandBuffer  Recorded,
                              std::uint32_t    SlotIndex,
                              std::uint32_t    LevelIndex);

    /// 🧩 Releases every span one part-built residency claimed, so a refusal retains nothing.
    void Abandon(CulledResidency& Abandoned);

    SpanSpace*          SpanEdge       = nullptr;   // [-] - borrowed; never owned
    ImageSpace*         ImageEdge      = nullptr;   // [-] - borrowed; never owned
    const TargetSpace*  TargetEdge     = nullptr;   // [-] - borrowed; never owned
    ShaderCodec*        ModuleEdge     = nullptr;   // [-] - borrowed; never owned
    DescriptorIndex*    DescriptorEdge = nullptr;   // [-] - borrowed; never owned
    ProgramIndex*       ProgramEdge    = nullptr;   // [-] - borrowed; never owned

    DepthReduction                  Chain              = {};                 // [-] - the level extents; holds no depth
    std::vector<CulledResidency>    Culled             = {};                 // [-] - one per resident partitioning
    std::vector<std::uint32_t>      LevelOffsets       = {};                 // [-] - three ordinals per level
    std::vector<std::uint32_t>      ReductionSpans     = {};                 // [-] - one per level per cycle slot
    std::uint32_t                   ChainSpan          = AbsentSpan;         // [-] - ChainTexels() reals
    std::uint32_t                   LevelExtentSpan    = AbsentSpan;         // [-] - the offsets, as the device reads them
    std::uint32_t                   ReductionLayout    = AbsentDescriptor;   // [-] - the reduction's three slots
    std::uint32_t                   OcclusionLayout    = AbsentDescriptor;   // [-] - the occlusion's seven
    std::vector<std::uint32_t>      ReductionReservations    = {};                 // [-] - one claim per level
    std::uint32_t                   ReductionProgram   = AbsentProgram;      // [-] - the level reduction
    std::uint32_t                   OcclusionProgram   = AbsentProgram;      // [-] - the partition test
    std::uint32_t                   ReductionModule    = AbsentModule;       // [-] - its lowered stream
    std::uint32_t                   OcclusionModule    = AbsentModule;       // [-] - likewise
    bool                            ReducedFor[static_cast<std::size_t>(RecordingSlotCount)] = {};
    bool                            ChainEverReduced   = false;              // [-] - ① may test against it
};

// 📐 The chain arithmetic and the level selection are integer throughout and therefore Exact; the reduction is a
//    minimum over ordinates the raster recorded and introduces no arithmetic of its own. `00` §3's transitivity
//    rule folds the two to the weaker, which is the Bounded the recorded depth already carries.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
