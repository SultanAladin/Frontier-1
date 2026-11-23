//============================================================================================================================================
//                                                            DISPLAYSCHEDULER.H
//============================================================================================================================================
// 🧩 The presentation chain and the pacing of it — image transitions ordered against a declared latency target.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"
#include "SlateVulkan/Device/CycleScheduler/Api/CycleScheduler.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE LATENCY TARGET
//------------------------------------------------------------------------------------------------------------------------

// 📝 No image; never a valid arrived ordinal. Sibling of `ImageSpace`'s `AbsentImage`.
inline constexpr std::uint32_t AbsentDisplayImage = 0xFFFFFFFFu;   // [-] - nothing has arrived

/// 🧩 What the artist is optimising for, which is what the pacing is chosen against.
/// note  🔴 `06` §2.1 settles that presentation is paced against a latency target and that `DisplayScheduler`
///        owns the choice. The choice is declared here as two named intents rather than as a vendor mode,
///        because the vendor's modes are three per driver and not all of them are present on every device —
///        a caller naming one directly has named something a second machine declines.
/// note  ⚠️ `Immediate` is not offered. A texturing application shows the artist a surface with a torn edge
///        halfway down it, and the tear is read as a defect in the brush rather than as an absent wait.
/// tag   guarantee
enum class LatencyIntent : std::uint32_t
{
    LowestLatency  = 0u,   // [-] - the stroke reaches the display as early as the device accepts
    SteadyPacing   = 1u,   // [-] - a regular interval, at the cost of one interval of latency
    IntentCount    = 2u    // [-] - the closed count, never an intent
};

/// 🧩 What one arrival delivers — which image the recording writes and how far behind the display it is.
/// note  🔴 `Reclaimed` is a member rather than a refusal. A chain the display has outgrown still delivers an
///        image this rotation, and the caller re-claims **after** presenting it; refusing instead would drop
///        the rotation that noticed, and the artist sees the resize as one stalled stroke.
/// tag   nonallocating, nonthrowing
struct AcquiredImage
{
    std::uint32_t  ImageIndex   = AbsentDisplayImage;   // [-]  - which chain image this rotation writes
    VkImage        Extent         = VK_NULL_HANDLE;       // [-]  - the vendor image, owned by the chain
    VkImageView    WholeView      = VK_NULL_HANDLE;       // [-]  - constructed here, reclaimed with the chain
    double         PacedInterval  = 0.0;                  // [ms] - measured between this arrival and the last
    bool           Reclaimed      = false;                // [-]  - the chain no longer matches the display
};

//------------------------------------------------------------------------------------------------------------------------
//                                                THE PRESENTATION CHAIN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The chain of display images, the surface format every target is claimed against, and the pacing.
/// note  🔴 This is `06` §2 ⑤ and ⑥ — the extent claimed and the presentation chain established. Until it
///        stood, `08` §2's `DisplaySurface` named a target with no format to claim it at:
///        `TargetSpace::Reserve` takes the display format as an operand, and `Carries` is where that operand
///        comes from. Re-deriving it at the claim site would let the chain and the targets disagree about one
///        format with nothing comparing them.
/// note  🔴 `CycleScheduler` already declares the two ordering points a chain needs — `ImageAvailable` signalled
///        by the display and awaited by the recording, `RecordingDone` signalled by the recording and awaited
///        by the display. Nothing here constructs an ordering point of its own; a second set would be one the
///        rotation does not know it is waiting on.
/// note  ⚠️ The chain's image count is the vendor's and is **not** `RecordingSlotCount`. The two are
///        independent — the rotation is how many recordings the host may write ahead, the chain is how many
///        images the display holds — and a claim sized against the wrong one is a per-slot resource that
///        aliases on whichever driver reports a different count.
/// tag   owning
class DisplayScheduler
{
public:

    // 📝 The interval a rotation is paced against when the artist asks for steady pacing. Sixty per second is
    //    the interval every display Slate targets can hold; a target derived from the display's reported rate
    //    would change under the artist when a window moved between two displays.
    static constexpr double PacedIntervalTarget = 1000.0 / 60.0;   // [ms] - one steady interval

    DisplayScheduler()                                   = default;
    DisplayScheduler(const DisplayScheduler&)            = delete;
    DisplayScheduler& operator=(const DisplayScheduler&) = delete;
    ~DisplayScheduler();

    /// 🧩 Establishes the chain against one surface, at one extent, for one declared latency intent.
    /// in    Exchange       [-]  the created device; borrowed and outlives this component
    /// in    Naming         [-]  names the chain and every view of it; borrowed and outlives this component
    /// in    Surface        [-]  what `WindowExchange::Convert` delivered; borrowed, reclaimed by its converter
    /// in    DisplayWidth   [px] the extent the window reports now
    /// in    DisplayHeight  [px] the extent the window reports now
    /// in    Intent         [-]  what the artist is optimising for
    /// out   Result        [-]  refuses with CapabilityAbsent when no device is active or the surface declares
    ///                           no format, ContentUnsupported for a zero or excessive extent, and
    ///                           ExtentExhausted when the device declines the chain
    /// post  `Carries` names the format every display-relative target is claimed at; the chain holds no image
    ///       the display has not handed back
    /// note  🔴 Rejected in full. A chain whose images were constructed and whose views were rejected leaves the
    ///        vendor holding images nothing references, and they are returned only when the surface is.
    /// note  🔴 `06` §7's diagnostic-name gate, discharged inside `Establish` so that `Reclaim` names the chain it
    ///        re-establishes too. The chain carries the establishment ordinal rather than no ordinal, because two
    ///        chains stand at once for the length of a resize — the retiring one is named as `oldSwapchain` while
    ///        the incoming one is constructed — and a report against either would otherwise read alike.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructDisplayScheduler(const VulkanExchange&       Exchange,
                            const DiagnosticExtension&  Naming,
                            VkSurfaceKHR                Surface,
                            std::uint32_t               DisplayWidth,
                            std::uint32_t               DisplayHeight,
                            LatencyIntent               Intent);

    /// 🧩 Re-establishes the chain at a new extent, retaining the format the targets were claimed at.
    /// in    DisplayWidth   [px] the arrived extent; an intermediate drag extent is the caller's to discard
    /// in    DisplayHeight  [px]
    /// out   Result        [-]  refuses as Construct does
    /// pre   🔴 the device is idle — every rotation that reads the old chain has completed
    /// note  🔴 `06` §7's extent gate is discharged by the **caller**, in one order: re-establish the chain
    ///        here, then `TargetSpace::Reclaim` at the same extent, then `AttachmentIndex::Derive` over the
    ///        re-claimed views. A chain re-established without the targets following is a display image at the
    ///        arrived extent composited from targets at the previous one, which reads as a shifted image rather
    ///        than as a resize that was half applied.
    /// note  ⚠️ A zero extent is rejected rather than deferred. A minimised window reports one, and the caller
    ///        stops rotating instead of establishing a chain no image can be claimed from.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Reclaim(std::uint32_t DisplayWidth, std::uint32_t DisplayHeight);

    /// 🧩 Takes the next display image, ordering its arrival against the standing cycle slot.
    /// in    Current  [-]  the cycle slot this image is recorded into; its `ImageAvailable` is signalled
    /// in    Timeline  [-]  measures the interval between this arrival and the last
    /// out   Result   [-]  refuses with CapabilityAbsent when no chain stands, RelationCyclic when an image is
    ///                      already taken, HostDenied when the display neither delivers an image nor reports
    ///                      the chain outgrown within the arrival ceiling, and DeviceLost when the device was
    ///                      lost; the chain is left standing for the recovery to reclaim
    /// post  the delivered ordinal is the caller's until `Present` returns it
    /// note  🔴 The arrival is ordered on `SlotIndex::ImageAvailable`, which the recording waits on before it
    ///        writes colour. An arrival ordered on nothing is a recording that writes an image the display is
    ///        still reading, and the artist sees the previous rotation's stroke tear through this one's.
    /// note  🔴 `Reclaimed` is delivered in two cases that differ in whether this rotation may proceed, and
    ///        `ImageIndex` is what says which. A chain the display has merely outgrown delivers a usable
    ///        image — present it, then re-establish. A chain it has retired delivers `AbsentDisplayImage` and no
    ///        view: there is nothing to record into, so the caller re-establishes and skips the rotation. A
    ///        caller reading `Reclaimed` alone would record into a null view on the second of the two.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<AcquiredImage> Await(const CycleSlot& Current, const TickSequence& Timeline);

    /// 🧩 Returns one taken image back to the display, ordered behind the recording that wrote it.
    /// in    Current      [-]  the same slot `Await` was given; its `RecordingDone` is awaited
    /// in    ImageIndex  [-]  what `Await` delivered
    /// out   Result       [-]  refuses with ContentUnsupported for an ordinal `Await` did not deliver, with
    ///                          HostDenied when the display declines it, and with DeviceLost when the device
    ///                          was lost; the ordinal is released to the display either way
    /// post  the ordinal is the display's again; `Current` is raised
    /// note  🔴 Awaits `RecordingDone` and never the completion. The completion is the host's fence and waiting
    ///        on it here would serialise the host against the device once per rotation — which is the whole
    ///        purpose of the recording slot count, spent.
    /// out   Result  [-]  delivers `true` when the chain still matches the display, `false` when the image
    ///                     was presented against a chain the display has outgrown; refuses only when nothing
    ///                     was presented at all
    /// note  🔴 ⚠️ The delivered VALUE is the re-establishment signal and a caller that reads only
    ///        `Resolved` will never rebuild. `VK_ERROR_OUT_OF_DATE_KHR` is a successful presentation
    ///        against a stale chain: refusing it would make a resize look like a lost rotation, and
    ///        delivering `true` for it — which this did — made the caller's re-establishment branch
    ///        unreachable, so a resize was rebuilt one tick late by the extent test or not at all.
    /// cost  🚩
    /// tag   api, nonthrowing
    [[nodiscard]] Deliver<bool> Present(const CycleSlot& Current, std::uint32_t ImageIndex);

    /// 🧩 What the surface carries, which is the format every display-relative target is claimed at.
    /// out   Format  [-]  VK_FORMAT_UNDEFINED before Construct delivered
    /// note  🔴 Read by `TargetSpace::Reserve` and by nothing that re-derives it. `08` §2 gives `DisplaySurface`
    ///        the format of the display rather than a declared one, and this is the only place that is known.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    VkFormat Carries() const;

    /// 🧩 The extent the chain stands at, which every display-relative target is claimed against.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t CurrentWidth() const;
    std::uint32_t CurrentHeight() const;

    /// 🧩 The minimum image count requested when the standing chain was created.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t MinimumChainImageCount() const;

    /// 🧩 How many images the chain actually holds — the vendor's count, never `RecordingSlotCount`.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t ChainImageCount() const;

    /// 🧩 The interval between the last two arrivals, for the pacing report `86` presents.
    /// out   Interval  [ms]  zero until two images have arrived
    /// note  ⚠️ Measured on the host between two arrivals and not on the device. It is what the artist waited,
    ///        which is the quantity the latency intent was chosen against; `HardwareMetrics` measures what the
    ///        device spent, and the two answer different questions.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    double PacedInterval() const;

    /// 🧩 How many images have been surrendered to the display since the chain was established.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t Current() const;

    /// 🧩 Destroys every view and the chain, and forgets the extent it stood at.
    /// pre   the device is idle
    /// note  The surface is not destroyed here. `WindowExchange::Reclaim` converted it and returns it; a chain
    ///       that destroyed the surface it was established against would reclaim what it borrowed.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Return();

private:

    // 📝 🔴 About two seconds, in the nanoseconds the vendor counts in, and deliberately the same figure
    //    `CycleScheduler::CompletionLimitNanoseconds` carries. The two bound the same failure from two sides —
    //    a device that stopped completing and a display that stopped delivering — and one of them longer than
    //    the other only decides which of the two reports it first.
    static constexpr std::uint64_t ArrivalLimitNanoseconds = 2000000000ull;   // [ns]

    /// 🧩 Scores the surface's declared formats and takes the one `08` §2's display target is claimed at.
    /// note  🔴 An unsigned-normalised format is taken over a sRGB-encoded one. `66` applies the OETF itself —
    ///        `08` §3 ⑧ is exposure, tone map and OETF in one recording — and a sRGB surface would apply it a
    ///        second time in hardware, which the artist reads as a washed-out surface rather than as a double
    ///        encoding.
    Deliver<VkSurfaceFormatKHR> ScoreFormat(VkSurfaceKHR Surface) const;

    /// 🧩 The vendor pacing one declared latency intent resolves to, from what the surface accepts.
    /// note  🔴 Every surface accepts `VK_PRESENT_MODE_FIFO_KHR` by declaration, so it is the fallback for both
    ///        intents and nothing here refuses over an absent mode.
    VkPresentModeKHR ScorePacing(VkSurfaceKHR Surface, LatencyIntent Intent) const;

    /// 🧩 Establishes the chain and its views at the standing extent, retiring whatever stood before.
    Deliver<bool> Establish();

    const VulkanExchange*      DeviceEdge       = nullptr;              // [-]  - borrowed; never owned
    const DiagnosticExtension* NamingEdge       = nullptr;              // [-]  - borrowed; never owned
    VkSurfaceKHR               DisplaySurface   = VK_NULL_HANDLE;       // [-]  - borrowed; reclaimed by `04`
    VkSwapchainKHR             DisplayChain     = VK_NULL_HANDLE;       // [-]  - the vendor spelling, verbatim
    std::vector<VkImage>       ChainImages      = {};                   // [-]  - the display's, never destroyed
    std::vector<VkImageView>   ChainViews       = {};                   // [-]  - constructed here and destroyed here
    VkFormat                   SurfaceCarries   = VK_FORMAT_UNDEFINED;  // [-]  - what every target is claimed at
    VkColorSpaceKHR            SurfaceEncoding  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR           ChainPacing      = VK_PRESENT_MODE_FIFO_KHR;
    LatencyIntent              DeclaredIntent   = LatencyIntent::SteadyPacing;
    std::uint32_t              ChainWidth         = 0u;                   // [px] - the extent the chain stands at
    std::uint32_t              ChainHeight        = 0u;                   // [px]
    std::uint32_t              MinimumChainImages = 0u;                   // [-]  - requested through minImageCount
    std::uint32_t              TakenIndex       = AbsentDisplayImage;   // [-]  - the image `Await` delivered
    std::uint32_t              EstablishedCount = 0u;                   // [-]  - chains stood so far; names this one
    TickPoint                  LastArrival      = {};                   // [ns] - when the previous image arrived
    double                     ArrivalInterval  = 0.0;                  // [ms] - between the last two arrivals
    std::uint64_t              ReturnedCount = 0u;                   // [-]  - images given to the display
};

}   // namespace Slate
