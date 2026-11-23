//============================================================================================================================================
//                                                               IMAGESPACE.H
//============================================================================================================================================
// 🧩 Device image extents — claimed against a declared shape, viewed once, and carrying the layout each one stands in.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateVulkan/Device/ByteSpace/Api/ByteSpace.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE DECLARED SHAPE
//------------------------------------------------------------------------------------------------------------------------

// 📝 No image; never a valid image ordinal. Sibling of `ByteSpace`'s `AbsentExtent` and `20`'s `AbsentTile`.
inline constexpr std::uint32_t AbsentImage = 0xFFFFFFFFu;   // [-] - the claim names no image

/// 🧩 What a claimant intends to do with an image, which is what its usage and its aspect follow from.
/// note  🔴 Declared rather than derived from the format. D32 is a depth attachment for `16` and a sampled
///        source for `60`, and an image claimed for one and used for the other is a validation error at the
///        recording site rather than at the claim — a long way from the declaration that caused it.
/// tag   guarantee
enum class ImageIntent : std::uint32_t
{
    ColourTarget    = 0u,   // [-] - written by a graphics recording, then sampled
    DepthTarget     = 1u,   // [-] - the depth attachment, then sampled by `60` and `30`
    ComputeWritable = 2u,   // [-] - a storage image a dispatch writes, then sampled
    SampledOnly     = 3u,   // [-] - transfer destination and sampled source; nothing writes it on the device
    IntentCount     = 4u    // [-] - the closed count, never an intent
};

/// 🧩 One image's declared shape, authored by the claimant and never inferred here.
/// note  ⚠️ `LevelCount` above one is the reduction chain `16` §2 walks for its predictor. It is declared at
///        the claim because the chain's extents are the image's, and an image claimed flat cannot grow one.
/// tag   nonallocating, nonthrowing
struct ImageShape
{
    VkFormat       Format      = VK_FORMAT_UNDEFINED;      // [-]  - the vendor spelling, verbatim
    std::uint32_t  Width       = 0u;                       // [px]
    std::uint32_t  Height      = 0u;                       // [px]
    std::uint32_t  LayerCount  = 1u;                       // [-]  - array layers; one for an ordinary target
    std::uint32_t  LevelCount  = 1u;                       // [-]  - reduction levels; one for an ordinary target
    ImageIntent    Intent      = ImageIntent::ColourTarget; // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  ONE CLAIMED IMAGE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a claimant is handed — the image, the whole-image view, and where it currently stands.
/// note  🔴 `CurrentLayout` is the component's record and not the caller's. It is amended by `Transition`
///        alone, because a recording that issues its own barrier has made a claim the record cannot see and
///        the next transition then barriers from a layout the image is not in.
/// tag   nonallocating, nonthrowing
struct ImageReservation
{
    VkImage        Extent         = VK_NULL_HANDLE;              // [-] - the vendor image
    VkImageView    WholeView      = VK_NULL_HANDLE;              // [-] - every level, every layer
    VkImageLayout  CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;   // [-] - what the last transition left it in
    ImageShape     Shape          = {};                          // [-] - as claimed, never as re-queried
    std::uint32_t  ImageIndex   = AbsentImage;                 // [-] - which slot Release returns it to
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE IMAGE INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every device image the engine holds, each sliced out of `ByteSpace` and each carrying its own layout.
/// note  🔴 This is where `08` §2's "nothing claims a format, an extent or memory" is closed. The shared
///        target set declares fifteen formats and their extent relations, and until something walked that
///        declaration and claimed against it the table was a document rather than a target.
/// note  ⚠️ An extent change reclaims and re-claims every display-relative image and touches no absolute
///        one — `06` §7's gate, enforced by the caller passing the relation, never re-derived here.
/// tag   owning
class ImageSpace
{
public:

    ImageSpace()                             = default;
    ImageSpace(const ImageSpace&)            = delete;
    ImageSpace& operator=(const ImageSpace&) = delete;
    ~ImageSpace();

    /// 🧩 Takes the device and the byte extents every claimed image is sliced from.
    /// in    Exchange     [-]  the created device; borrowed and outlives this component
    /// in    BackingSpace [-]  where image bytes come from; borrowed and outlives this component
    /// in    Naming       [-]  names every claimed image and every view over it; borrowed and outlives this
    /// out   Result      [-]  refuses with CapabilityAbsent when no device is active
    /// note  🔴 `06` §7's diagnostic-name gate. The image and its views are named separately because they are
    ///        separate vendor objects, and the driver reports a view's error against the view — an unnamed
    ///        view under a named image reports as an address beside a name, which reads as two objects.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ConstructImageSpace(const VulkanExchange&      Exchange,
                            ByteSpace&                 BackingSpace,
                            const DiagnosticExtension& Naming);

    /// 🧩 Reservations one image of the declared shape, slices its bytes, and constructs its whole-image view.
    /// in    Declared  [-]  the shape; nothing about it is inferred from the format
    /// out   Result   [-]  refuses with ExtentExhausted when no bytes remain, ContentUnsupported for a
    ///                      zero extent or a format the device declines for the declared intent
    /// post  the image stands in VK_IMAGE_LAYOUT_UNDEFINED and is transitioned before first use
    /// note  🔴 Rejected in full. An image whose bytes were claimed and whose view was rejected leaves a
    ///        vendor allocation nothing holds a reference to, and it is reclaimed only at device teardown.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<ImageReservation> Reserve(const ImageShape& Declared);

    /// 🧩 Records the barrier that carries one image from where it stands to where it is next read.
    /// in    Recorded    [-]  the command being recorded into
    /// in    ImageIndex[-]  the claim's ordinal; the record is amended, not the caller's copy
    /// in    Incoming    [-]  the layout the next recording requires
    /// out   Result     [-]  refuses with ContentUnsupported for an unclaimed ordinal
    /// post  the recorded layout is the incoming one; a repeat transition to the same layout is a no-op
    /// note  🔴 `08` §4: no contributing document issues a layout transition directly. The barrier is
    ///        derived from the declared reads and writes, and this is the one place it is recorded.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Transition(VkCommandBuffer Recorded, std::uint32_t ImageIndex, VkImageLayout Incoming);

    /// 🧩 The current record for one claimed image, including the layout the last transition left it in.
    /// out   Result  [-]  refuses with ContentUnsupported for an unclaimed ordinal
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<ImageReservation> Current(std::uint32_t ImageIndex) const;

    /// 🧩 Constructs a view over one reduction level, for the chain `16` §2 walks a level at a time.
    /// in    ImageIndex [-]  a claimed image whose LevelCount accepts the level
    /// in    LevelIndex [-]  the level; zero is the full extent
    /// out   Result      [-]  refuses with ContentUnsupported outside the declared level count
    /// note  The view is owned here and reclaimed with the image. A caller destroying one leaves the index
    ///        holding a handle the vendor has already reused.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<VkImageView> LevelView(std::uint32_t ImageIndex, std::uint32_t LevelIndex);

    /// 🧩 Destroys one image, every view over it, and returns its bytes.
    /// pre   the device is idle, or no recording still in the rotation reads it
    /// cost  🚩
    /// tag   api, nonthrowing
    void Release(std::uint32_t ImageIndex);

    /// 🧩 Releases every claimed image.
    /// pre   the device is idle
    /// cost  🔴
    /// tag   api, nonthrowing
    void Reclaim();

    std::uint32_t ReservedCount() const;
    VkDeviceSize  ReservedBytes() const;

private:

    struct HeldImage
    {
        VkImage                   Extent         = VK_NULL_HANDLE;              // [-] - the vendor image
        VkImageView               WholeView      = VK_NULL_HANDLE;              // [-] - every level, every layer
        std::vector<VkImageView>  LevelViews     = {};                          // [-] - one per reduction level
        ByteReservation                 Backing        = {};                          // [-] - the bytes it occupies
        ImageShape                Shape          = {};                          // [-] - as claimed
        VkImageLayout             CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;   // [-] - amended by Transition
        bool                      SlotOccupied   = false;                       // [-] - false once released
    };

    /// 🧩 What the declared intent requires of the image, as the vendor spells it.
    static VkImageUsageFlags   UsageOf(ImageIntent Intent);
    static VkImageAspectFlags  AspectOf(ImageIntent Intent);

    /// 🧩 What one declared intent is named in the driver's text, so a claim's name states what writes it.
    static const char* NameOf(ImageIntent Intent);

    const VulkanExchange*       DeviceEdge   = nullptr;   // [-] - borrowed; never owned
    ByteSpace*                  BackingBytes = nullptr;   // [-] - borrowed; never owned
    const DiagnosticExtension*  NamingEdge   = nullptr;   // [-] - borrowed; never owned
    std::vector<HeldImage>      Images       = {};        // [-] - released slots are reused, never erased
};

}   // namespace Slate
