//============================================================================================================================================
//                                                             RENDERSCHEDULE.H
//============================================================================================================================================
// 🧩 What is recorded in a cycle slot, in what order, and against which shared targets.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateVulkan/Device/ImageSpace/Api/ImageSpace.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHARED TARGETS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every shared target the schedule declares. Nothing invents a target another already produces.
/// note  Declared as one closed enumeration so that the producer and amender lists below are total.
/// tag   guarantee
enum class SharedTarget : std::uint32_t
{
    DepthSurface           = 0u,   // [-] - D32, display extent
    VisibilityIndex        = 1u,   // [-] - R32G32 unsigned integer, display extent
    OccupancySurface       = 2u,   // [-] - R8, display extent
    MotionSurface          = 3u,   // [-] - R16G16 real, display extent
    OcclusionSurface       = 4u,   // [-] - R8, half display extent
    DirectOcclusionSurface = 5u,   // [-] - RGBA8; four illuminants, per DirectOcclusionCapacity
    TransmissionIndex      = 6u,   // [-] - R32G32 unsigned integer × TransmissionDepth, display extent
    RadianceSurface        = 7u,   // [-] - RGBA16 real, display extent
    ReflectionSurface      = 8u,   // [-] - RGBA16 real, half display extent
    AccumulationSurface    = 9u,   // [-] - RGBA16 real, display extent
    DisplaySurface         = 10u,  // [-] - display format and extent
    OutlineSurface         = 11u,  // [-] - R8, display extent
    TransmittanceSurface   = 12u,  // [-] - RGBA16 real, 256 × 64 — resident, 128 KiB
    MultiScatterSurface    = 13u,  // [-] - RGBA16 real, 32 × 32 — resident, 8 KiB
    SkyViewSurface         = 14u,  // [-] - RGBA16 real, 192 × 108 — resident, 162 KiB
    TargetCount            = 15u   // [-] - the closed count, never a target
};

/// 🧩 How a target's extent relates to the display extent.
/// note  ⚠️ A display-relative target is reclaimed and re-claimed on every extent change; an absolute one
///       is never touched by a resize. `06` §4.1 gates that both ways.
/// tag   guarantee
enum class ExtentRelation : std::uint32_t
{
    DisplayRelative  = 0u,   // [-] - exactly the display extent
    FractionOfDisplay = 1u,  // [-] - a declared fraction of it
    Absolute         = 2u    // [-] - a fixed extent, independent of the display
};

/// 🧩 The relation one target's extent bears to the display extent.
/// note  The table is total over `SharedTarget` and is declared once beside the schedule. A caller re-deriving
///       a relation from a format or an extent has derived it from the wrong operand.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ExtentRelation RelationOfTarget(SharedTarget Target);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE CLAIMED SET
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the fifteen declared targets are claimed as — one image ordinal each, against one display extent.
/// note  🔴 This is `08` §2's "nothing claims a format, an extent or memory", closed. The table above declared
///       fifteen formats and their extent relations and nothing walked it; `TargetSpace` is what walks it.
/// note  🔴 `06` §7's extent gate: a display extent change reclaims and re-claims **every** display-relative
///       and fraction-of-display target and touches no absolute one. `Reclaim` refuses to hold a persistent
///       extent across the change, and an intermediate drag extent is discarded by the caller not queued here.
/// tag   owning
class TargetSpace
{
public:

    TargetSpace()                              = default;
    TargetSpace(const TargetSpace&)            = delete;
    TargetSpace& operator=(const TargetSpace&) = delete;

    /// 🧩 Reservations every declared target against one display extent and one display format.
    /// in    Images        [-]  where the images are claimed; borrowed and outlives this component
    /// in    DisplayWidth  [px] the display extent this claim is relative to
    /// in    DisplayHeight [px] the display extent this claim is relative to
    /// in    DisplayFormat [-]  what the display surface itself carries; the vendor spelling
    /// out   Result       [-]  refuses with ContentUnsupported for a zero or excessive extent, and with
    ///                          whatever `ImageSpace` rejected when a target could not be claimed
    /// post  every target carries an image ordinal, or nothing does — rejected in full
    /// note  🔴 Rejected in full. A half-claimed target set is one where `08` §3's ordering reads a target that
    ///        was never claimed, and the recording site meets it as a null view rather than as this refusal.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Reserve(ImageSpace&    Images,
                        std::uint32_t  DisplayWidth,
                        std::uint32_t  DisplayHeight,
                        VkFormat       DisplayFormat);

    /// 🧩 Re-claims every display-relative and fraction-of-display target against a new display extent.
    /// in    DisplayWidth  [px] the arrived extent; an intermediate drag extent is the caller's to discard
    /// in    DisplayHeight [px]
    /// out   Result       [-]  refuses as Reserve does; the absolute targets stand untouched either way
    /// pre   🔴 the device is idle — every rotation that reads the old targets has completed
    /// note  🔴 `06` §7's fourth gate, verbatim: **every** display-relative target is recreated and no
    ///        persistent extent is carried across. Re-claiming a subset is how one target keeps the previous
    ///        extent and reads as a shifted image nobody attributes to the resize.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Reclaim(std::uint32_t DisplayWidth, std::uint32_t DisplayHeight);

    /// 🧩 The image one declared target was claimed as.
    /// out   Result  [-]  refuses with ContentUnsupported when the target is unclaimed
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<ImageReservation> Resolve(SharedTarget Target) const;

    /// 🧩 The image ordinal one target was claimed as, for the transition `ImageSpace` records.
    /// out   Result  [-]  refuses with ContentUnsupported when the target is unclaimed
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<std::uint32_t> IndexOf(SharedTarget Target) const;

    /// 🧩 Releases every claimed target and forgets the display extent they were claimed against.
    /// pre   the device is idle
    /// cost  🚩
    /// tag   api, nonthrowing
    void Release();

private:

    /// 🧩 The shape one target is claimed at, derived from its relation and the standing display extent.
    /// out   Result  [-]  refuses with ContentUnsupported for an extent of zero or above the ceiling
    Deliver<ImageShape> ShapeOf(SharedTarget Target) const;

    ImageSpace*    ImageEdge      = nullptr;                // [-] - borrowed; never owned
    std::uint32_t  ReservedFor[static_cast<std::size_t>(SharedTarget::TargetCount)] = {};
    bool           TargetReserved[static_cast<std::size_t>(SharedTarget::TargetCount)] = {};
    std::uint32_t  CurrentWidth  = 0u;                     // [px] - the display extent claimed against
    std::uint32_t  CurrentHeight = 0u;                     // [px]
    VkFormat       DisplayCarries = VK_FORMAT_UNDEFINED;    // [-]  - what the display surface itself carries
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      RECORDINGS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a recording contributes. Authored once by the contributing document, consulted by the orderer.
/// tag   guarantee
enum class RecordingCommand : std::uint32_t
{
    GraphicsRecording = 0u,   // [-] - a graphics recording
    ComputeDispatch   = 1u    // [-] - a compute dispatch
};

/// 🧩 One declared recording.
/// note  🔴 A recording with a capability requirement and no substitution is rejected at bring-up. An
///       absent capability must degrade to something, and choosing that something belongs to the
///       contributing document rather than to a branch invented at the recording site.
/// tag   owning
struct DeclaredRecording
{
    const char*                Identity            = "";                              // [-] - unique; used in metrics
    std::vector<SharedTarget>  Reads               = {};                              // [-] - consumed targets
    std::vector<SharedTarget>  Produces            = {};                              // [-] - targets written whole
    std::vector<SharedTarget>  Amends              = {};                              // [-] - targets modified in place
    RecordingCommand           Command             = RecordingCommand::GraphicsRecording;
    bool                       CapabilityRequired  = false;                           // [-] - needs a scored capability
    const char*                Substitution        = "";                              // [-] - what runs instead
    bool                       DisplayReferred     = false;                           // [-] - recorded after the tone line

    // 🔴 `08` §2's amendment list, as a number. `RenderSchedule` derives its order from declared reads and
    //    writes, and two recordings that **amend** one target declare no read of each other — so nothing in the
    //    derivation separates them and the order falls to whichever was contributed first. `62` §6 requires the
    //    transmission amendment before `30`'s reflection, and `30` §5 requires it from the other side; a
    //    contribution accident deciding it is a reflection that does not show a pane of glass, on some builds.
    // 📝 Zero for a recording that amends nothing. Ties fall back to contribution order, which is what every
    //    recording contributed before this field existed relied on.
    std::uint32_t              AmendmentIndex    = 0u;                              // [-] - lower records first
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SCHEDULE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The ordered recordings of one rotation, fixed at bring-up and merely executed per rotation.
/// note  🔴 The term is `RenderSchedule`. "Frame graph" is not a synonym: `Frame` is banned and "graph"
///       implies a solved dependency structure Slate does not build. The recordings and the target set are
///       both known at bring-up, so the ordering is fixed there too.
/// tag   owning
class RenderSchedule
{
public:

    /// 🧩 Contributes one recording to the schedule.
    /// in    Incoming [-]  the declaration the contributing document authored
    /// out   Result  [-]  refuses when a capability is required with no substitution, or when the
    ///                     contribution produces a target another recording already produces
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Contribute(const DeclaredRecording& Incoming);

    /// 🧩 Fixes the ordering. Derived from the declared reads and writes, never hand-written.
    /// out   Result  [-]  refuses when a target is read by a recording ordered before its producer, or
    ///                     when anything scene-referred is ordered after the display-referred line
    /// post  the ordering is immutable until the next bring-up
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Fix();

    /// 🧩 The recordings, in the order Fix derived.
    /// pre   Fix delivered
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<DeclaredRecording>& Ordered() const;

private:

    std::vector<DeclaredRecording>  ContributedOrder;                            // [-] - as contributed
    std::vector<DeclaredRecording>  OrderedRecordings;                           // [-] - as Fix derived
    RecordingIdentity               ProducerOf[static_cast<std::size_t>(SharedTarget::TargetCount)] = {};
    bool                            OrderingFixed = false;                       // [-] - Fix has delivered
};

}   // namespace Slate
