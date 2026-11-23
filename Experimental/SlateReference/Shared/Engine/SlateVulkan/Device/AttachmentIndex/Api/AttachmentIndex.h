//============================================================================================================================================
//                                                             ATTACHMENTINDEX.H
//============================================================================================================================================
// 🧩 The classic render constructs `06` §2.1 settled on, declared over the shared targets and re-derived on an extent change.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateVulkan/Device/ImageSpace/Api/ImageSpace.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT IS DECLARED
//------------------------------------------------------------------------------------------------------------------------

// 📝 No construct; never a valid construct ordinal. Sibling of `ProgramIndex`'s `AbsentProgram`.
inline constexpr std::uint32_t AbsentConstruct = 0xFFFFFFFFu;   // [-] - the resolution names no construct

// 📝 A construct declaring no depth target. Spelled rather than reached for through `SharedTarget::TargetCount`,
//    which is the closed count and not a target — using the count as an absence marker is how a later reader
//    comes to believe it is one.
inline constexpr std::uint32_t DepthTargetAbsent = 0xFFFFFFFFu;   // [-] - the construct resolves no depth

/// 🧩 One render construct, as the recording that draws through it declares it.
/// note  🔴 The colour order **is** the fragment's output order. A shader writing its second output to what
///       this run lists third writes it to another target entirely, and both targets then carry plausible
///       imagery — which is the reason the run is declared rather than derived from the produced set, whose
///       order is the contributing document's convenience.
/// note  ⚠️ `DepthTarget` carries `DepthTargetAbsent` for a construct that resolves no depth. It is the
///       ordinal of a `SharedTarget` otherwise, and `RelationOfTarget` is not consulted here — every target a
///       construct spans stands at the display extent by declaration.
/// tag   owning
struct ConstructDeclaration
{
    std::vector<SharedTarget>  ColourTargets = {};                  // [-] - in the fragment's own output order
    std::uint32_t              DepthTarget   = DepthTargetAbsent;   // [-] - a SharedTarget ordinal, or absent
};

//------------------------------------------------------------------------------------------------------------------------
//                                                THE CONSTRUCTED SPAN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a recording is handed — the construct it opens, the span it opens over, and that span's extent.
/// note  ⚠️ The extent is delivered rather than re-derived, because the recording writes it into the display
///       ordinates it records and a second derivation of one extent is `00` §2's case. It is the extent the
///       last `Derive` was performed against and not what the display currently reports.
/// tag   nonallocating, nonthrowing
struct ConstructedSpan
{
    VkRenderPass   RenderConstruct = VK_NULL_HANDLE;   // [-] - the vendor construct; programs are built against it
    VkFramebuffer  SpannedTargets  = VK_NULL_HANDLE;   // [-] - the vendor span over the claimed views
    std::uint32_t  SpannedWidth    = 0u;               // [px] - what Derive was performed against
    std::uint32_t  SpannedHeight   = 0u;               // [px]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE ATTACHMENT INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every render construct the engine declares, and the span each one covers the claimed targets with.
/// note  🔴 `06` §2.1 settles **classic render construct, not dynamic**. Every graphics program in Slate is
///       constructed against a construct declared here, and a recording reaching for a dynamic rendering
///       declaration instead has taken a decision `06` already took.
/// note  🔴 Every attachment is declared to stand in the attachment layout on entry **and** on exit, so the
///       vendor performs no implicit transition. `ImageSpace::Transition` stays the one place a layout
///       changes — a construct that transitioned on its own would leave `ImageSpace`'s record naming a layout
///       the image is not in, and the next barrier would then be registered from the wrong one.
/// note  ⚠️ The constructs outlive an extent change and the spans do not. `06` §7 reclaims and re-claims every
///       display-relative target on a resize, which invalidates every view a span was derived over; the
///       construct describes formats alone and is untouched. `Derive` is what re-covers them.
/// tag   owning
class AttachmentIndex
{
public:

    AttachmentIndex()                                  = default;
    AttachmentIndex(const AttachmentIndex&)            = delete;
    AttachmentIndex& operator=(const AttachmentIndex&) = delete;
    ~AttachmentIndex();

    /// 🧩 Takes the device and the claimed target set every construct is declared over.
    /// in    Exchange  [-]  the created device; borrowed and outlives this component
    /// in    Reserved   [-]  where the target views come from; borrowed and outlives this component
    /// out   Result   [-]  refuses with CapabilityAbsent when no device is active
    /// post  no construct is declared
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ConstructAttachmentIndex(const VulkanExchange& Exchange, const TargetSpace& Reserved);

    /// 🧩 Declares one render construct, returning the ordinal every later resolution names it by.
    /// in    Declaring  [-]  the colour targets in output order, and the depth target or its absence
    /// out   Result    [-]  refuses with ContentUnsupported for a declaration spanning nothing at all or
    ///                       naming an unclaimed target, and with HostDenied when the device declines it
    /// post  the construct stands; no span is derived until Derive is called
    /// note  📝 Declared from the claimed targets' formats rather than from `08` §2's table a second time.
    ///       The table is what `TargetSpace` claimed against, and re-reading it here would let a construct
    ///       and a claim come to disagree about one target's format with nothing comparing them.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare(const ConstructDeclaration& Declaring);

    /// 🧩 Covers every declared construct's targets at one display extent, replacing whatever stood before.
    /// in    DisplayWidth   [px] the extent the targets were last claimed against
    /// in    DisplayHeight  [px]
    /// out   Result        [-]  refuses with ContentUnsupported for a zero extent or an unclaimed target,
    ///                           and with HostDenied when the device declines a span
    /// pre   🔴 the device is idle and `TargetSpace` has re-claimed at this extent
    /// post  every declared construct carries a span at this extent, or none does — rejected in full
    /// note  🔴 Rejected in full and derived for **every** construct, not the ones an extent change touched.
    ///       `06` §7's gate is that no persistent extent survives a resize, and a span retained because its
    ///       construct "looked unaffected" is exactly such an extent.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Derive(std::uint32_t DisplayWidth, std::uint32_t DisplayHeight);

    /// 🧩 The construct and the span one ordinal names, for the recording that opens it.
    /// in    ConstructIndex  [-]  an ordinal this component registered
    /// out   Result           [-]  refuses with ContentUnsupported for an ordinal naming no construct, and
    ///                              with ExtentExhausted before Derive has covered it
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<ConstructedSpan> Resolve(std::uint32_t ConstructIndex) const;

    /// 🧩 The construct alone, for `ProgramIndex` constructing a program before any span is derived.
    /// out   Result  [-]  refuses with ContentUnsupported for an ordinal naming no construct
    /// note  📝 Separate from `Resolve` because a program is constructed at bring-up, before the first extent
    ///       is known, and only the formats enter that construction. Requiring a derived span to construct a
    ///       program would order the two the wrong way round.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<VkRenderPass> ConstructOf(std::uint32_t ConstructIndex) const;

    /// 🧩 Destroys every span and leaves the constructs standing, ahead of a re-claim at a new extent.
    /// pre   the device is idle
    /// cost  🚩
    /// tag   api, nonthrowing
    void Release();

    /// 🧩 Destroys every span and every construct.
    /// pre   the device is idle and no program constructed against one is still recorded
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

    std::uint32_t DeclaredCount() const;
    bool          SpansDerived() const;

private:

    struct HeldConstruct
    {
        VkRenderPass               RenderConstruct = VK_NULL_HANDLE;                  // [-] - the vendor construct
        VkFramebuffer              SpannedTargets  = VK_NULL_HANDLE;                  // [-] - the vendor span
        std::vector<SharedTarget>  ColourTargets   = {};                              // [-] - as declared, in order
        std::uint32_t              DepthTarget     = DepthTargetAbsent;               // [-] - as declared
    };

    /// 🧩 Where one attachment stands throughout the construct, so that the vendor transitions nothing.
    /// out   VkImageLayout  [-]  the attachment layout its aspect requires
    static VkImageLayout LayoutOf(bool DepthAspect);

    const VulkanExchange*       DeviceEdge    = nullptr;   // [-] - borrowed; never owned
    const TargetSpace*          TargetEdge    = nullptr;   // [-] - borrowed; never owned
    std::vector<HeldConstruct>  Constructs    = {};        // [-] - every declared construct
    std::uint32_t               DerivedWidth  = 0u;        // [px] - what the standing spans were derived against
    std::uint32_t               DerivedHeight = 0u;        // [px]
    bool                        SpanCurrent  = false;     // [-]  - Derive has covered every construct
};

}   // namespace Slate
