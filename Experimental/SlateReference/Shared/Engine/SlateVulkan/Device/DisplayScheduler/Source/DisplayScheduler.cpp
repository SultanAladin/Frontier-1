//============================================================================================================================================
//                                                           DISPLAYSCHEDULER.CPP
//============================================================================================================================================
// 🧩 The scored format, the established chain, the ordered arrival, and the surrender back to the display.

#include "SlateVulkan/Device/DisplayScheduler/Api/DisplayScheduler.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SCORING
//------------------------------------------------------------------------------------------------------------------------

Deliver<VkSurfaceFormatKHR> DisplayScheduler::ScoreFormat(VkSurfaceKHR Surface) const
{
    std::uint32_t DeclaredCount = 0u;
    vkGetPhysicalDeviceSurfaceFormatsKHR(DeviceEdge->ScoredDevice(), Surface, &DeclaredCount, nullptr);

    if (DeclaredCount == 0u)
    {
        return Deliver<VkSurfaceFormatKHR>::Refuse(
            { RefusalReason::CapabilityAbsent, "the surface declares no format" });
    }

    std::vector<VkSurfaceFormatKHR> Declared(DeclaredCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(DeviceEdge->ScoredDevice(), Surface, &DeclaredCount, Declared.data());

    // 🔴 An unsigned-normalised format is taken over a sRGB-encoded one. `08` §3 ⑧ is exposure, tone map and
    //    OETF in one recording — `66` applies the transfer function itself — and a sRGB surface applies it a
    //    second time in hardware. The artist reads the result as a washed-out surface rather than as one
    //    encoding performed twice, which is why the preference is stated here and not left to arrival order.
    for (const VkSurfaceFormatKHR& Candidate : Declared)
    {
        if (Candidate.format == VK_FORMAT_B8G8R8A8_UNORM || Candidate.format == VK_FORMAT_R8G8B8A8_UNORM)
            return Deliver<VkSurfaceFormatKHR>::Result(Candidate);
    }

    // 📝 A surface declaring only sRGB-encoded formats is served rather than rejected. `66` reads the format it
    //    is claimed at, so the double encoding is a reported quality departure and not an absent image.
    return Deliver<VkSurfaceFormatKHR>::Result(Declared[0]);
}

VkPresentModeKHR DisplayScheduler::ScorePacing(VkSurfaceKHR Surface, LatencyIntent Intent) const
{
    std::uint32_t DeclaredCount = 0u;
    vkGetPhysicalDeviceSurfacePresentModesKHR(DeviceEdge->ScoredDevice(), Surface, &DeclaredCount, nullptr);

    // 🔴 Every surface accepts FIFO by declaration, so it is the fallback for both intents and nothing here
    //    refuses over an absent mode. `06` §2.1 gives this component the choice; a refusal would have made the
    //    choice the vendor's.
    if (DeclaredCount == 0u)
        return VK_PRESENT_MODE_FIFO_KHR;

    std::vector<VkPresentModeKHR> Declared(DeclaredCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(DeviceEdge->ScoredDevice(), Surface, &DeclaredCount, Declared.data());

    // 📝 Lowest latency takes the relaxed mode, which replaces a waiting image rather than queueing behind it —
    //    the stroke reaches the display as early as the device accepts, at the cost of a rotation whose work is
    //    discarded. Steady pacing stays on FIFO, where every image is shown for one interval.
    // ⚠️ `VK_PRESENT_MODE_IMMEDIATE_KHR` is offered by neither intent. It shows the artist a surface with a torn
    //    edge halfway down it, and the tear reads as a defect in the brush rather than as an absent wait.
    const VkPresentModeKHR Preferred = (Intent == LatencyIntent::LowestLatency)
                                     ? VK_PRESENT_MODE_MAILBOX_KHR
                                     : VK_PRESENT_MODE_FIFO_KHR;

    for (const VkPresentModeKHR Candidate : Declared)
    {
        if (Candidate == Preferred)
            return Preferred;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DisplayScheduler::ConstructDisplayScheduler(const VulkanExchange&       Exchange,
                                          const DiagnosticExtension&  Naming,
                                          VkSurfaceKHR                Surface,
                                          std::uint32_t               DisplayWidth,
                                          std::uint32_t               DisplayHeight,
                                          LatencyIntent               Intent)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE || Surface == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active, or no surface" });

    DeviceEdge     = &Exchange;
    NamingEdge     = &Naming;
    DisplaySurface = Surface;
    DeclaredIntent = Intent;

    const Deliver<VkSurfaceFormatKHR> Scored = ScoreFormat(Surface);

    if (!Scored.Resolved)
        return Deliver<bool>::Refuse(Scored.Error);

    SurfaceCarries  = Scored.Resolve().format;
    SurfaceEncoding = Scored.Resolve().colorSpace;
    ChainPacing     = ScorePacing(Surface, Intent);

    ChainWidth  = DisplayWidth;
    ChainHeight = DisplayHeight;

    return Establish();
}

Deliver<bool> DisplayScheduler::Reclaim(std::uint32_t DisplayWidth, std::uint32_t DisplayHeight)
{
    if (DeviceEdge == nullptr || DisplaySurface == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no chain was ever established" });

    // 📝 🔴 The format is **retained** rather than re-scored. Every display-relative target was claimed at it,
    //    and a re-scored format that came back different would leave `TargetSpace` holding targets claimed at
    //    the previous one — with nothing comparing the two, because `TargetSpace::Reclaim` takes the extent
    //    alone by declaration and reads the format it was first claimed with.
    ChainWidth  = DisplayWidth;
    ChainHeight = DisplayHeight;

    return Establish();
}

Deliver<bool> DisplayScheduler::Establish()
{
    if (ChainWidth == 0u || ChainHeight == 0u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a zero extent was asked for; a minimised window reports one" });
    }

    if (ChainWidth > DisplayExtentLimit || ChainHeight > DisplayExtentLimit)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the extent exceeds DisplayExtentLimit" });

    VkSurfaceCapabilitiesKHR Accepted = {};

    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(DeviceEdge->ScoredDevice(), DisplaySurface, &Accepted) != VK_SUCCESS)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "the surface declares no capability" });

    // 📝 The extent is clamped into what the surface accepts rather than rejected against it. A window manager
    //    that reports an extent the surface will not carry is one Slate meets on a tiling desktop, and the image
    //    the artist wants is the clamped one rather than none.
    std::uint32_t EstablishedWidth  = ChainWidth;
    std::uint32_t EstablishedHeight = ChainHeight;

    if (Accepted.currentExtent.width != 0xFFFFFFFFu)
    {
        EstablishedWidth  = Accepted.currentExtent.width;
        EstablishedHeight = Accepted.currentExtent.height;
    }

    if (EstablishedWidth  < Accepted.minImageExtent.width)   EstablishedWidth  = Accepted.minImageExtent.width;
    if (EstablishedHeight < Accepted.minImageExtent.height)  EstablishedHeight = Accepted.minImageExtent.height;
    if (EstablishedWidth  > Accepted.maxImageExtent.width)   EstablishedWidth  = Accepted.maxImageExtent.width;
    if (EstablishedHeight > Accepted.maxImageExtent.height)  EstablishedHeight = Accepted.maxImageExtent.height;

    if (EstablishedWidth == 0u || EstablishedHeight == 0u)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the surface accepts no extent; the window carries none" });
    }

    // 🔴 One image beyond the minimum the surface declares. At exactly the minimum the display holds every
    //    image it owns, and the arrival then waits for one to be released before the recording may begin —
    //    which serialises the whole rotation on the display regardless of the pacing chosen.
    std::uint32_t RequestedCount = Accepted.minImageCount + 1u;

    if (Accepted.maxImageCount != 0u && RequestedCount > Accepted.maxImageCount)
        RequestedCount = Accepted.maxImageCount;

    VkSwapchainCreateInfoKHR ChainDeclaration = {};
    ChainDeclaration.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ChainDeclaration.surface          = DisplaySurface;
    ChainDeclaration.minImageCount    = RequestedCount;
    ChainDeclaration.imageFormat      = SurfaceCarries;
    ChainDeclaration.imageColorSpace  = SurfaceEncoding;
    ChainDeclaration.imageExtent      = { EstablishedWidth, EstablishedHeight };
    ChainDeclaration.imageArrayLayers = 1u;

    // 📝 A colour attachment and a transfer destination. `08` §3 ⑫ amends `DisplaySurface` with the interface
    //    and ⑬ presents it, so the chain image is written as an attachment; the transfer admission is what lets
    //    `66` resolve into it directly on a device where the two formats agree.
    ChainDeclaration.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // 📝 🔴 Exclusive by declaration, which `06` §2.1 already settled: one graphics queue, so no chain image is
    //    ever read by a second family. A concurrent declaration would name families that do not exist here.
    ChainDeclaration.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ChainDeclaration.preTransform     = Accepted.currentTransform;
    ChainDeclaration.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ChainDeclaration.presentMode      = ChainPacing;
    ChainDeclaration.clipped          = VK_TRUE;

    // 🔴 The retiring chain is named, so the display keeps showing its last image while this one is built. A
    //    resize that established from null instead shows the artist one black image per drag increment.
    ChainDeclaration.oldSwapchain = DisplayChain;

    VkSwapchainKHR Established = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(DeviceEdge->ActiveDevice(), &ChainDeclaration, nullptr, &Established) != VK_SUCCESS)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the device rejected the presentation chain" });
    }

    // 📝 The retiring chain and its views are destroyed only once the new one stands. Destroying them first
    //    would leave the display with nothing to show for the duration of the construction above, and the
    //    `oldSwapchain` operand would name an object the vendor has already returned.
    Return();

    DisplayChain = Established;
    ChainWidth   = EstablishedWidth;
    ChainHeight  = EstablishedHeight;

    // 📝 🔴 `06` §7's diagnostic-name gate. The ordinal counts establishments rather than anything the chain
    //    holds, because a resize retires one chain and stands another under the same member — and the two are
    //    told apart in the driver's text only by which establishment named them. Raised before the name is
    //    declared so that the first chain is the nought-th. The refusal is discarded for `ByteSpace`'s reason.
    const std::uint32_t ChainIndex = EstablishedCount++;

    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_SWAPCHAIN_KHR,
                        reinterpret_cast<std::uint64_t>(DisplayChain),
                        "DisplayScheduler presentation chain",
                        ChainIndex));

    std::uint32_t ImageCount = 0u;
    vkGetSwapchainImagesKHR(DeviceEdge->ActiveDevice(), DisplayChain, &ImageCount, nullptr);

    if (ImageCount < RequestedCount)
    {
        vkDestroySwapchainKHR(DeviceEdge->ActiveDevice(), DisplayChain, nullptr);
        DisplayChain = VK_NULL_HANDLE;
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the chain holds fewer images than requested" });
    }

    MinimumChainImages = RequestedCount;
    ChainImages.assign(ImageCount, VK_NULL_HANDLE);
    vkGetSwapchainImagesKHR(DeviceEdge->ActiveDevice(), DisplayChain, &ImageCount, ChainImages.data());

    ChainViews.assign(ImageCount, VK_NULL_HANDLE);

    for (std::uint32_t ImageIndex = 0u; ImageIndex < ImageCount; ++ImageIndex)
    {
        VkImageViewCreateInfo ViewDeclaration = {};
        ViewDeclaration.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ViewDeclaration.image                           = ChainImages[ImageIndex];
        ViewDeclaration.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ViewDeclaration.format                          = SurfaceCarries;
        ViewDeclaration.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ViewDeclaration.subresourceRange.levelCount     = 1u;
        ViewDeclaration.subresourceRange.layerCount     = 1u;

        if (vkCreateImageView(DeviceEdge->ActiveDevice(), &ViewDeclaration, nullptr, &ChainViews[ImageIndex])
            != VK_SUCCESS)
        {
            // 🔴 Rejected in full. A chain half-viewed is one where `08` §3 ⑬ presents an image whose view the
            //    recording met as a null handle, and the vendor reports that at the draw rather than here.
            Return();
            return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the device rejected a chain image view" });
        }

        // 📝 🔴 `06` §7's gate reaches the view, which Slate constructs. Named by the chain and the image both,
        //    because the ordinal a display hands back is not the cycle slot — `Await` takes whichever image the
        //    display releases — so a name carrying only one of the two cannot say which chain a stale view came
        //    from after a resize.
        Discard(NamingEdge->Declare(VK_OBJECT_TYPE_IMAGE_VIEW,
                            reinterpret_cast<std::uint64_t>(ChainViews[ImageIndex]),
                            "DisplayScheduler chain view",
                            ChainIndex * ImageCount + ImageIndex));

        // 📝 The image is named although Slate did not create it, which is past what the gate asks. Every layout
        //    transition `08` §3 ⑬ records is reported against the image and never against the view, so an unnamed
        //    one leaves the single most frequent presentation error the one report that carries no name.
        Discard(NamingEdge->Declare(VK_OBJECT_TYPE_IMAGE,
                            reinterpret_cast<std::uint64_t>(ChainImages[ImageIndex]),
                            "DisplayScheduler chain image",
                            ChainIndex * ImageCount + ImageIndex));
    }

    TakenIndex     = AbsentDisplayImage;
    LastArrival      = {};
    ArrivalInterval  = 0.0;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ARRIVAL
//------------------------------------------------------------------------------------------------------------------------

Deliver<AcquiredImage> DisplayScheduler::Await(const CycleSlot& Current, const TickSequence& Timeline)
{
    if (DeviceEdge == nullptr || DisplayChain == VK_NULL_HANDLE)
        return Deliver<AcquiredImage>::Refuse({ RefusalReason::CapabilityAbsent, "no chain is established" });

    if (TakenIndex != AbsentDisplayImage)
    {
        return Deliver<AcquiredImage>::Refuse(
            { RefusalReason::RelationCyclic, "an image is already taken and has not been presented" });
    }

    std::uint32_t Sampled = 0u;

    // 🔴 The arrival is ordered on the standing slot's `ImageAvailable`, which the recording waits on before it
    //    writes colour. `CycleScheduler` already owns that point; a second one constructed here would be one the
    //    rotation does not know it is waiting on, and the recording would write an image the display still reads.
    // 📝 The ceiling is finite for `CycleScheduler::Await`'s reason, restated: an indefinite wait against a lost
    //    device is a host that stops with no report, and `06` §7 requires the loss reported upward before
    //    anything is destroyed — which cannot happen from inside a wait that never returns. A timeout here is
    //    rejected as the display having neither delivered nor reported, which is what an unresponsive one is.
    const VkResult Reached = vkAcquireNextImageKHR(DeviceEdge->ActiveDevice(),
                                                  DisplayChain,
                                                  ArrivalLimitNanoseconds,
                                                  Current.ImageAvailable,
                                                  VK_NULL_HANDLE,
                                                  &Sampled);

    // ⚠️ `VK_ERROR_OUT_OF_DATE_KHR` delivers **no** image — nothing was taken, and the arrived ordinal names
    //    nothing. `VK_SUBOPTIMAL_KHR` delivers one that is usable this rotation. The two are reported through
    //    one member because both mean the chain no longer matches the display, and they differ in whether this
    //    rotation may proceed — which is what `ImageIndex` says.
    if (Reached == VK_ERROR_OUT_OF_DATE_KHR)
    {
        AcquiredImage Outgrown;
        Outgrown.Reclaimed = true;
        return Deliver<AcquiredImage>::Result(Outgrown);
    }

    // 🔴 `06` §7: the loss is reported upward and the chain is left standing. Re-establishing it here would
    //    destroy a chain against a device that can no longer be asked to, and `06` §4.2's recovery is what
    //    reclaims both in an order this component cannot know.
    if (Reached == VK_ERROR_DEVICE_LOST)
    {
        return Deliver<AcquiredImage>::Refuse(
            { RefusalReason::DeviceLost, "the device was lost awaiting a display image" });
    }

    if (Reached != VK_SUCCESS && Reached != VK_SUBOPTIMAL_KHR)
    {
        return Deliver<AcquiredImage>::Refuse(
            { RefusalReason::HostDenied, "the display neither delivered an image nor reported the chain outgrown" });
    }

    if (Sampled >= static_cast<std::uint32_t>(ChainViews.size()))
    {
        return Deliver<AcquiredImage>::Refuse(
            { RefusalReason::ContentUnsupported, "the display named an image the chain does not hold" });
    }

    const TickPoint AcquiredAt = Timeline.Advance();

    // 📝 Measured on the host between two arrivals, which is what the artist waited — the quantity the latency
    //    intent was chosen against. `HardwareMetrics` measures what the device spent, and the two answer
    //    different questions; a report carrying only the second one says a stalled display is running fast.
    if (LastArrival.Index != 0u)
        ArrivalInterval = TickSequence::Span(LastArrival, AcquiredAt);

    LastArrival  = AcquiredAt;
    TakenIndex = Sampled;

    AcquiredImage Delivered;
    Delivered.ImageIndex  = Sampled;
    Delivered.Extent        = ChainImages[Sampled];
    Delivered.WholeView     = ChainViews[Sampled];
    Delivered.PacedInterval = ArrivalInterval;
    Delivered.Reclaimed     = (Reached == VK_SUBOPTIMAL_KHR);

    return Deliver<AcquiredImage>::Result(Delivered);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE SURRENDER
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DisplayScheduler::Present(const CycleSlot& Current, std::uint32_t ImageIndex)
{
    if (DeviceEdge == nullptr || DisplayChain == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no chain is established" });

    if (ImageIndex == AbsentDisplayImage || ImageIndex != TakenIndex)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "that image was not the one taken" });

    VkPresentInfoKHR ReturnDeclaration = {};
    ReturnDeclaration.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    // 🔴 Awaits `RecordingDone` and never the slot's completion. The completion is the host's fence, and waiting
    //    on it here would serialise the host against the device once per rotation — which is the whole purpose
    //    of the recording slot count, spent to order something a semaphore already orders on the device.
    ReturnDeclaration.waitSemaphoreCount = 1u;
    ReturnDeclaration.pWaitSemaphores    = &Current.RecordingDone;
    ReturnDeclaration.swapchainCount     = 1u;
    ReturnDeclaration.pSwapchains        = &DisplayChain;
    ReturnDeclaration.pImageIndices      = &ImageIndex;

    const VkResult PresentResult = vkQueuePresentKHR(DeviceEdge->GraphicsQueue(), &ReturnDeclaration);

    // 📝 The ordinal is released before the result is read. The display owns the image either way — an outgrown
    //    chain has still consumed it — and retaining the ordinal would make the next arrival refuse for a
    //    presentation that did in fact happen.
    TakenIndex = AbsentDisplayImage;
    ++ReturnedCount;

    // ⚠️ An outgrown chain delivers rather than refuses. The image was presented, and refusing would make the
    //    caller treat a resize as a lost rotation.
    // 🔴 The delivered value says whether the chain still MATCHES the display, and the three accepted results
    //    do not agree on that. Reporting all three as `true` made the caller's re-establishment unreachable:
    //    a resize was then rebuilt only by the next tick's extent test, one tick late, and a chain that
    //    reported outgrown without the window extent moving was never rebuilt at all.
    if (PresentResult == VK_SUCCESS || PresentResult == VK_SUBOPTIMAL_KHR)
        return Deliver<bool>::Result(true);

    // 🔴 Current, but the chain no longer matches. Delivered `false` so the caller re-establishes rather
    //    than treating the rotation as lost.
    if (PresentResult == VK_ERROR_OUT_OF_DATE_KHR)
        return Deliver<bool>::Result(false);

    if (PresentResult == VK_ERROR_DEVICE_LOST)
        return Deliver<bool>::Refuse({ RefusalReason::DeviceLost, "the device was lost presenting an image" });

    return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the display rejected the image" });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE READINGS
//------------------------------------------------------------------------------------------------------------------------

VkFormat      DisplayScheduler::Carries() const        { return SurfaceCarries;   }
std::uint32_t DisplayScheduler::CurrentWidth() const  { return ChainWidth;       }
std::uint32_t DisplayScheduler::CurrentHeight() const { return ChainHeight;      }
double        DisplayScheduler::PacedInterval() const  { return ArrivalInterval;  }
std::uint64_t DisplayScheduler::Current() const      { return ReturnedCount; }

std::uint32_t DisplayScheduler::MinimumChainImageCount() const
{
    return MinimumChainImages;
}

std::uint32_t DisplayScheduler::ChainImageCount() const
{
    return static_cast<std::uint32_t>(ChainImages.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void DisplayScheduler::Return()
{
    if (DeviceEdge != nullptr && DeviceEdge->ActiveDevice() != VK_NULL_HANDLE)
    {
        const VkDevice Active = DeviceEdge->ActiveDevice();

        for (VkImageView& Held : ChainViews)
        {
            if (Held != VK_NULL_HANDLE)
            {
                vkDestroyImageView(Active, Held, nullptr);
                Held = VK_NULL_HANDLE;
            }
        }

        if (DisplayChain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(Active, DisplayChain, nullptr);
            DisplayChain = VK_NULL_HANDLE;
        }
    }

    // 📝 The images themselves are never destroyed. They belong to the chain and the display returns them with
    //    it; destroying one is a double free the validation layer reports against the presentation instead.
    ChainImages.clear();
    ChainViews.clear();
    MinimumChainImages = 0u;
    TakenIndex       = AbsentDisplayImage;
}

DisplayScheduler::~DisplayScheduler()
{
    Return();
}

}   // namespace Slate
