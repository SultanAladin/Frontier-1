//============================================================================================================================================
//                                                              IMAGESPACE.CPP
//============================================================================================================================================
// 🧩 The image claim, the one place a layout transition is recorded, and the reclamation that returns both.

#include "SlateVulkan/Device/ImageSpace/Api/ImageSpace.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                               WHAT THE INTENT REQUIRES
//------------------------------------------------------------------------------------------------------------------------

VkImageUsageFlags ImageSpace::UsageOf(ImageIntent Intent)
{
    // 📝 Every intent carries SAMPLED and TRANSFER_SRC. `86` reads any target back for a diagnostic capture and
    //    `08` §3.1's display-referred line samples what the line above it wrote, so declaring them per intent
    //    would mean re-claiming a target the first time a diagnostic asked for it.
    const VkImageUsageFlags Universal = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    switch (Intent)
    {
        case ImageIntent::ColourTarget:
            return Universal | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

        case ImageIntent::DepthTarget:
            return Universal | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        case ImageIntent::ComputeWritable:
            return Universal | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        case ImageIntent::SampledOnly:
        default:
            return Universal | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
}

VkImageAspectFlags ImageSpace::AspectOf(ImageIntent Intent)
{
    // 📝 🔴 Depth-only. No target in `08` §2 carries stencil, and naming the stencil aspect on a format that
    //    has none is a validation error at every barrier rather than at the claim that chose the aspect.
    return Intent == ImageIntent::DepthTarget ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ImageSpace::ConstructImageSpace(const VulkanExchange&      Exchange,
                                    ByteSpace&                 BackingSpace,
                                    const DiagnosticExtension& Naming)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE || Exchange.ScoredDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge   = &Exchange;
    BackingBytes = &BackingSpace;
    NamingEdge   = &Naming;

    return Deliver<bool>::Result(true);
}

const char* ImageSpace::NameOf(ImageIntent Intent)
{
    // 📝 The intent rather than the target. `TargetSpace` knows which of `08` §2's fifteen an ordinal is and
    //    this does not, so the name states what the device may do with the image and the ordinal states which
    //    one it is — and `TargetSpace::Resolve` is what carries the reader from one to the other.
    switch (Intent)
    {
        case ImageIntent::ColourTarget:    return "ImageSpace colour image";
        case ImageIntent::DepthTarget:     return "ImageSpace depth image";
        case ImageIntent::ComputeWritable: return "ImageSpace storage image";

        case ImageIntent::SampledOnly:
        default:                           return "ImageSpace sampled image";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE CLAIM
//------------------------------------------------------------------------------------------------------------------------

Deliver<ImageReservation> ImageSpace::Reserve(const ImageShape& Declared)
{
    if (DeviceEdge == nullptr || BackingBytes == nullptr)
        return Deliver<ImageReservation>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    if (Declared.Width == 0u || Declared.Height == 0u)
        return Deliver<ImageReservation>::Refuse({ RefusalReason::ContentUnsupported, "an image of zero extent" });

    if (Declared.Format == VK_FORMAT_UNDEFINED)
        return Deliver<ImageReservation>::Refuse({ RefusalReason::ContentUnsupported, "an image of no declared format" });

    if (Declared.LayerCount == 0u || Declared.LevelCount == 0u)
        return Deliver<ImageReservation>::Refuse({ RefusalReason::ContentUnsupported, "an image of no layer or no level" });

    if (Declared.Intent == ImageIntent::IntentCount)
        return Deliver<ImageReservation>::Refuse({ RefusalReason::ContentUnsupported, "no such intent" });

    const VkDevice Active = DeviceEdge->ActiveDevice();

    // 🔴 Scored before anything is created. The alternative is a vendor error at vkCreateImage, which names
    //    the call and not the format, and `08` §5's substitution then has no operand to substitute against.
    VkFormatProperties FormatDeclaration = {};
    vkGetPhysicalDeviceFormatProperties(DeviceEdge->ScoredDevice(), Declared.Format, &FormatDeclaration);

    if ((FormatDeclaration.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0u)
    {
        return Deliver<ImageReservation>::Refuse(
            { RefusalReason::ContentUnsupported, "the device declines the declared format for an optimal image" });
    }

    VkImageCreateInfo ImageDeclaration = {};
    ImageDeclaration.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ImageDeclaration.imageType         = VK_IMAGE_TYPE_2D;
    ImageDeclaration.format            = Declared.Format;
    ImageDeclaration.extent.width      = Declared.Width;
    ImageDeclaration.extent.height     = Declared.Height;
    ImageDeclaration.extent.depth      = 1u;
    ImageDeclaration.mipLevels         = Declared.LevelCount;
    ImageDeclaration.arrayLayers       = Declared.LayerCount;
    ImageDeclaration.samples           = VK_SAMPLE_COUNT_1_BIT;
    ImageDeclaration.tiling            = VK_IMAGE_TILING_OPTIMAL;
    ImageDeclaration.usage             = UsageOf(Declared.Intent);
    ImageDeclaration.sharingMode       = VK_SHARING_MODE_EXCLUSIVE;
    ImageDeclaration.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage Incoming = VK_NULL_HANDLE;

    if (vkCreateImage(Active, &ImageDeclaration, nullptr, &Incoming) != VK_SUCCESS)
        return Deliver<ImageReservation>::Refuse({ RefusalReason::ExtentExhausted, "the device rejected the image" });

    // 📝 The requirement is read from the created image and never computed from the shape. Tiling, alignment
    //    and the admissible residencies are the vendor's, and a computed figure is right on one driver.
    VkMemoryRequirements Required = {};
    vkGetImageMemoryRequirements(Active, Incoming, &Required);

    // 🔴 `06` §7: a target is committed rather than discretionary. `08` §2's set is the working set for every
    //    recording in the ordering, and evicting one of them mid-ordering is a recording reading a released span.
    const Deliver<ByteReservation> Backing = BackingBytes->Reserve(Required.size,
                                                           Required.alignment,
                                                           ExtentResidency::DeviceLocal,
                                                           ReservationCondition::Committed);

    if (!Backing.Resolved)
    {
        vkDestroyImage(Active, Incoming, nullptr);
        return Deliver<ImageReservation>::Refuse(Backing.Error);
    }

    const ByteReservation Sliced = Backing.Resolve();

    if (vkBindImageMemory(Active, Incoming, Sliced.BackingExtent, Sliced.ByteOffset) != VK_SUCCESS)
    {
        BackingBytes->Release(Sliced);
        vkDestroyImage(Active, Incoming, nullptr);

        return Deliver<ImageReservation>::Refuse(
            { RefusalReason::ContentUnsupported, "the device failed to bind the claimed span to the image" });
    }

    VkImageViewCreateInfo ViewDeclaration            = {};
    ViewDeclaration.sType                            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewDeclaration.image                            = Incoming;
    ViewDeclaration.viewType                         = Declared.LayerCount > 1u ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                                                : VK_IMAGE_VIEW_TYPE_2D;
    ViewDeclaration.format                           = Declared.Format;
    ViewDeclaration.subresourceRange.aspectMask      = AspectOf(Declared.Intent);
    ViewDeclaration.subresourceRange.baseMipLevel    = 0u;
    ViewDeclaration.subresourceRange.levelCount      = Declared.LevelCount;
    ViewDeclaration.subresourceRange.baseArrayLayer  = 0u;
    ViewDeclaration.subresourceRange.layerCount      = Declared.LayerCount;

    VkImageView Whole = VK_NULL_HANDLE;

    if (vkCreateImageView(Active, &ViewDeclaration, nullptr, &Whole) != VK_SUCCESS)
    {
        vkDestroyImage(Active, Incoming, nullptr);
        BackingBytes->Release(Sliced);

        return Deliver<ImageReservation>::Refuse({ RefusalReason::ContentUnsupported, "the device rejected the image view" });
    }

    HeldImage Taken;
    Taken.Extent         = Incoming;
    Taken.WholeView      = Whole;
    Taken.LevelViews.assign(Declared.LevelCount, VK_NULL_HANDLE);
    Taken.Backing        = Sliced;
    Taken.Shape          = Declared;
    Taken.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Taken.SlotOccupied   = true;

    // 📝 A released slot is reused rather than erased, so that an ordinal a contributing document recorded
    //    against one rotation never names a different image the next.
    std::uint32_t Index = AbsentImage;

    for (std::size_t Candidate = 0u; Candidate < Images.size(); ++Candidate)
    {
        if (!Images[Candidate].SlotOccupied)
        {
            Images[Candidate] = Taken;
            Index           = static_cast<std::uint32_t>(Candidate);
            break;
        }
    }

    if (Index == AbsentImage)
    {
        Images.push_back(Taken);
        Index = static_cast<std::uint32_t>(Images.size() - 1u);
    }

    // 📝 🔴 `06` §7's diagnostic-name gate. The image and its whole-image view are separate vendor objects and
    //    are named separately, both by the ordinal the claimant resolves them by. The refusals are discarded
    //    for `ByteSpace`'s reason — an unnamed image is still the image the claimant asked for.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_IMAGE,
                        reinterpret_cast<std::uint64_t>(Incoming),
                        NameOf(Declared.Intent),
                        Index));

    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_IMAGE_VIEW,
                        reinterpret_cast<std::uint64_t>(Whole),
                        "ImageSpace whole-image view",
                        Index));

    ImageReservation Handed;
    Handed.Extent         = Incoming;
    Handed.WholeView      = Whole;
    Handed.CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Handed.Shape          = Declared;
    Handed.ImageIndex   = Index;

    return Deliver<ImageReservation>::Result(Handed);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TRANSITION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 The stage and the access a layout is reached at, derived from the layout alone. A finer derivation
    //    would take the reading recording as well, and `08` §4 declares reads and writes per target rather
    //    than per recording — so the coarse pair is what the declaration can actually supply.
    void ReachedAt(VkImageLayout Current, VkPipelineStageFlags& Stage, VkAccessFlags& Access)
    {
        switch (Current)
        {
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                Stage  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                Access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                return;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
                Stage  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                Access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                return;

            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                Stage  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                Access = VK_ACCESS_SHADER_READ_BIT;
                return;

            case VK_IMAGE_LAYOUT_GENERAL:
                Stage  = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                Access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                return;

            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                Stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
                Access = VK_ACCESS_TRANSFER_READ_BIT;
                return;

            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                Stage  = VK_PIPELINE_STAGE_TRANSFER_BIT;
                Access = VK_ACCESS_TRANSFER_WRITE_BIT;
                return;

            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                Stage  = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                Access = 0u;
                return;

            case VK_IMAGE_LAYOUT_UNDEFINED:
            default:
                Stage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                Access = 0u;
                return;
        }
    }
}

Deliver<bool> ImageSpace::Transition(VkCommandBuffer Recorded, std::uint32_t ImageIndex, VkImageLayout Incoming)
{
    if (Recorded == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no recording to record the barrier into" });

    if (static_cast<std::size_t>(ImageIndex) >= Images.size() || !Images[ImageIndex].SlotOccupied)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no image stands at that ordinal" });

    HeldImage& Held = Images[ImageIndex];

    // 📝 A repeat transition is delivered rather than rejected. `08` §3's ordering has several recordings
    //    declaring the same read of the same target, and refusing the second would make the contribution
    //    order a correctness condition the ordering does not promise.
    if (Held.CurrentLayout == Incoming)
        return Deliver<bool>::Result(true);

    VkPipelineStageFlags DepartingStage  = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags IncomingStage   = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkAccessFlags        DepartingAccess = 0u;
    VkAccessFlags        IncomingAccess  = 0u;

    ReachedAt(Held.CurrentLayout, DepartingStage, DepartingAccess);
    ReachedAt(Incoming, IncomingStage, IncomingAccess);

    VkImageMemoryBarrier Carried                = {};
    Carried.sType                               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    Carried.oldLayout                           = Held.CurrentLayout;
    Carried.newLayout                           = Incoming;
    Carried.srcQueueFamilyIndex                 = VK_QUEUE_FAMILY_IGNORED;
    Carried.dstQueueFamilyIndex                 = VK_QUEUE_FAMILY_IGNORED;
    Carried.image                               = Held.Extent;
    Carried.srcAccessMask                       = DepartingAccess;
    Carried.dstAccessMask                       = IncomingAccess;
    Carried.subresourceRange.aspectMask         = AspectOf(Held.Shape.Intent);
    Carried.subresourceRange.baseMipLevel       = 0u;
    Carried.subresourceRange.levelCount         = Held.Shape.LevelCount;
    Carried.subresourceRange.baseArrayLayer     = 0u;
    Carried.subresourceRange.layerCount         = Held.Shape.LayerCount;

    vkCmdPipelineBarrier(Recorded, DepartingStage, IncomingStage, 0u, 0u, nullptr, 0u, nullptr, 1u, &Carried);

    // 🔴 Amended at the recording and not at the submission. The ordering records every barrier before any of
    //    them executes, so a record amended at submission would derive every later barrier from the layout
    //    the image stood in one rotation ago.
    Held.CurrentLayout = Incoming;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT IS CLAIMED
//------------------------------------------------------------------------------------------------------------------------

Deliver<ImageReservation> ImageSpace::Current(std::uint32_t ImageIndex) const
{
    if (static_cast<std::size_t>(ImageIndex) >= Images.size() || !Images[ImageIndex].SlotOccupied)
        return Deliver<ImageReservation>::Refuse({ RefusalReason::ContentUnsupported, "no image stands at that ordinal" });

    const HeldImage& Held = Images[ImageIndex];

    ImageReservation Reported;
    Reported.Extent         = Held.Extent;
    Reported.WholeView      = Held.WholeView;
    Reported.CurrentLayout = Held.CurrentLayout;
    Reported.Shape          = Held.Shape;
    Reported.ImageIndex   = ImageIndex;

    return Deliver<ImageReservation>::Result(Reported);
}

Deliver<VkImageView> ImageSpace::LevelView(std::uint32_t ImageIndex, std::uint32_t LevelIndex)
{
    if (DeviceEdge == nullptr)
        return Deliver<VkImageView>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    if (static_cast<std::size_t>(ImageIndex) >= Images.size() || !Images[ImageIndex].SlotOccupied)
        return Deliver<VkImageView>::Refuse({ RefusalReason::ContentUnsupported, "no image stands at that ordinal" });

    HeldImage& Held = Images[ImageIndex];

    if (LevelIndex >= Held.Shape.LevelCount)
        return Deliver<VkImageView>::Refuse({ RefusalReason::ContentUnsupported, "the level is outside the declared count" });

    // 📝 Constructed on first ask and held for the image's life. `16` §2's reduction walks the chain once per
    //    rotation, and a view constructed per walk is a vendor object created and destroyed inside the
    //    rotation that reads it.
    if (Held.LevelViews[LevelIndex] != VK_NULL_HANDLE)
        return Deliver<VkImageView>::Result(Held.LevelViews[LevelIndex]);

    VkImageViewCreateInfo ViewDeclaration            = {};
    ViewDeclaration.sType                            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ViewDeclaration.image                            = Held.Extent;
    ViewDeclaration.viewType                         = Held.Shape.LayerCount > 1u ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                                                  : VK_IMAGE_VIEW_TYPE_2D;
    ViewDeclaration.format                           = Held.Shape.Format;
    ViewDeclaration.subresourceRange.aspectMask      = AspectOf(Held.Shape.Intent);
    ViewDeclaration.subresourceRange.baseMipLevel    = LevelIndex;
    ViewDeclaration.subresourceRange.levelCount      = 1u;
    ViewDeclaration.subresourceRange.baseArrayLayer  = 0u;
    ViewDeclaration.subresourceRange.layerCount      = Held.Shape.LayerCount;

    VkImageView Constructed = VK_NULL_HANDLE;

    if (vkCreateImageView(DeviceEdge->ActiveDevice(), &ViewDeclaration, nullptr, &Constructed) != VK_SUCCESS)
    {
        return Deliver<VkImageView>::Refuse(
            { RefusalReason::ContentUnsupported, "the device rejected a view over the declared level" });
    }

    Held.LevelViews[LevelIndex] = Constructed;

    // 📝 🔴 `06` §7's gate reaches the level view too. It is named by its level rather than by its image,
    //    because the chain `16` §2 walks constructs one per level over a single image — an ordinal naming the
    //    image would give every view in the chain one name, and the driver's text could not say which level
    //    the error was raised against.
    Discard(NamingEdge->Declare(VK_OBJECT_TYPE_IMAGE_VIEW,
                        reinterpret_cast<std::uint64_t>(Constructed),
                        "ImageSpace level view",
                        LevelIndex));

    return Deliver<VkImageView>::Result(Constructed);
}

std::uint32_t ImageSpace::ReservedCount() const
{
    std::uint32_t Current = 0u;

    for (const HeldImage& Held : Images)
    {
        if (Held.SlotOccupied)
            ++Current;
    }

    return Current;
}

VkDeviceSize ImageSpace::ReservedBytes() const
{
    VkDeviceSize Taken = 0u;

    for (const HeldImage& Held : Images)
    {
        if (Held.SlotOccupied)
            Taken += Held.Backing.ByteSpan;
    }

    return Taken;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void ImageSpace::Release(std::uint32_t ImageIndex)
{
    if (DeviceEdge == nullptr || static_cast<std::size_t>(ImageIndex) >= Images.size())
        return;

    HeldImage& Held = Images[ImageIndex];

    if (!Held.SlotOccupied)
        return;

    const VkDevice Active = DeviceEdge->ActiveDevice();

    if (Active != VK_NULL_HANDLE)
    {
        // 📝 Every view before the image. A view outliving the image it was constructed over is a dangling
        //    vendor reference the validation layer reports at the next unrelated destruction.
        for (VkImageView& Level : Held.LevelViews)
        {
            if (Level != VK_NULL_HANDLE)
            {
                vkDestroyImageView(Active, Level, nullptr);
                Level = VK_NULL_HANDLE;
            }
        }

        if (Held.WholeView != VK_NULL_HANDLE)
            vkDestroyImageView(Active, Held.WholeView, nullptr);

        if (Held.Extent != VK_NULL_HANDLE)
            vkDestroyImage(Active, Held.Extent, nullptr);
    }

    if (BackingBytes != nullptr)
        BackingBytes->Release(Held.Backing);

    Held = HeldImage{};
}

void ImageSpace::Reclaim()
{
    for (std::size_t Index = 0u; Index < Images.size(); ++Index)
        Release(static_cast<std::uint32_t>(Index));

    Images.clear();
}

ImageSpace::~ImageSpace()
{
    Reclaim();
}

}   // namespace Slate
