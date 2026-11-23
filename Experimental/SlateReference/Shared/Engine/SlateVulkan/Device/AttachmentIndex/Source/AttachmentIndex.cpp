//============================================================================================================================================
//                                                            ATTACHMENTINDEX.CPP
//============================================================================================================================================
// 🧩 The construct declared from the claimed formats, the span derived over the claimed views, and the two reclamations.

#include "SlateVulkan/Device/AttachmentIndex/Api/AttachmentIndex.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

VkImageLayout AttachmentIndex::LayoutOf(bool DepthAspect)
{
    return DepthAspect ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                       : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

Deliver<bool> AttachmentIndex::ConstructAttachmentIndex(const VulkanExchange& Exchange, const TargetSpace& Reserved)
{
    if (Exchange.ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device is active" });

    DeviceEdge = &Exchange;
    TargetEdge = &Reserved;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DECLARATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> AttachmentIndex::Declare(const ConstructDeclaration& Declaring)
{
    if (DeviceEdge == nullptr)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::CapabilityAbsent, "no device was taken" });

    if (Declaring.ColourTargets.empty() && Declaring.DepthTarget == DepthTargetAbsent)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "a render construct spanning no target at all" });
    }

    std::vector<VkAttachmentDescription> Described;
    std::vector<VkAttachmentReference>   ColourRead;

    Described.reserve(Declaring.ColourTargets.size() + 1u);
    ColourRead.reserve(Declaring.ColourTargets.size());

    // 🔴 Cleared on entry and retained on exit, for every colour attachment. `16` §5's unoccupied class is
    //    dispatched over pixels no surface resolved to, and that class is recognised by what the clear wrote —
    //    a target whose previous rotation's contents survived would carry last rotation's surfaces at exactly
    //    the pixels this rotation resolved nothing at, and they would shade as though still visible.
    for (const SharedTarget Colour : Declaring.ColourTargets)
    {
        const Deliver<ImageReservation> Current = TargetEdge->Resolve(Colour);

        if (!Current.Resolved)
            return Deliver<std::uint32_t>::Refuse(Current.Error);

        VkAttachmentDescription Description = {};
        Description.format                  = Current.Resolve().Shape.Format;
        Description.samples                 = VK_SAMPLE_COUNT_1_BIT;
        Description.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Description.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
        Description.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Description.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Description.initialLayout           = LayoutOf(false);
        Description.finalLayout             = LayoutOf(false);

        VkAttachmentReference Reference = {};
        Reference.attachment            = static_cast<std::uint32_t>(Described.size());
        Reference.layout                = LayoutOf(false);

        Described.push_back(Description);
        ColourRead.push_back(Reference);
    }

    VkAttachmentReference DepthRead = {};
    const bool            DepthDeclared = Declaring.DepthTarget != DepthTargetAbsent;

    if (DepthDeclared)
    {
        const Deliver<ImageReservation> Current =
            TargetEdge->Resolve(static_cast<SharedTarget>(Declaring.DepthTarget));

        if (!Current.Resolved)
            return Deliver<std::uint32_t>::Refuse(Current.Error);

        // 📝 Cleared to `FarPlaneDepth` by the recording that opens the construct, not by a magnitude declared
        //    here. The construct states that a clear happens; what it clears to is the reversed-depth
        //    convention `Foundation/` holds, and stating it twice is how the two come to differ.
        VkAttachmentDescription Description = {};
        Description.format                  = Current.Resolve().Shape.Format;
        Description.samples                 = VK_SAMPLE_COUNT_1_BIT;
        Description.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        Description.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
        Description.stencilLoadOp           = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        Description.stencilStoreOp          = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        Description.initialLayout           = LayoutOf(true);
        Description.finalLayout             = LayoutOf(true);

        DepthRead.attachment = static_cast<std::uint32_t>(Described.size());
        DepthRead.layout     = LayoutOf(true);

        Described.push_back(Description);
    }

    // 📝 One recorded division and no more. `08` §3's ordering is between recordings and not within one, so a
    //    construct carrying several divisions would hold an ordering the schedule cannot see — and `08` §6's
    //    gate is that the order is derived from declared reads and writes rather than hand-written.
    VkSubpassDescription Division = {};
    Division.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    Division.colorAttachmentCount = static_cast<std::uint32_t>(ColourRead.size());
    Division.pColorAttachments    = ColourRead.empty() ? nullptr : ColourRead.data();
    Division.pDepthStencilAttachment = DepthDeclared ? &DepthRead : nullptr;

    VkRenderPassCreateInfo Declaration = {};
    Declaration.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    Declaration.attachmentCount        = static_cast<std::uint32_t>(Described.size());
    Declaration.pAttachments           = Described.data();
    Declaration.subpassCount           = 1u;
    Declaration.pSubpasses             = &Division;

    VkRenderPass Constructed = VK_NULL_HANDLE;

    if (vkCreateRenderPass(DeviceEdge->ActiveDevice(), &Declaration, nullptr, &Constructed) != VK_SUCCESS)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::HostDenied, "the device rejected the render construct" });
    }

    HeldConstruct Held;
    Held.RenderConstruct = Constructed;
    Held.ColourTargets   = Declaring.ColourTargets;
    Held.DepthTarget     = Declaring.DepthTarget;

    const std::uint32_t ConstructIndex = static_cast<std::uint32_t>(Constructs.size());

    Constructs.push_back(Held);

    // 📝 A construct declared after the spans were derived leaves them incomplete rather than stale. The
    //    standing declaration is what `Resolve` refuses against, so the caller meets a refusal naming this
    //    construct rather than a span covering every construct but the newest.
    SpanCurrent = false;

    return Deliver<std::uint32_t>::Result(ConstructIndex);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DERIVATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> AttachmentIndex::Derive(std::uint32_t DisplayWidth, std::uint32_t DisplayHeight)
{
    if (DeviceEdge == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device was taken" });

    if (DisplayWidth == 0u || DisplayHeight == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a display extent of no interior" });

    // 🔴 Derived whole into a standing run before anything replaces what stands. A span constructed part of
    //    the way through and then rejected would leave half the constructs covering this extent and half
    //    covering the previous one, and a recording opening one of each writes two images at two extents into
    //    one target.
    std::vector<VkFramebuffer> Incoming;
    Incoming.reserve(Constructs.size());

    for (const HeldConstruct& Held : Constructs)
    {
        std::vector<VkImageView> Covered;
        Covered.reserve(Held.ColourTargets.size() + 1u);

        bool Rejected = false;

        for (const SharedTarget Colour : Held.ColourTargets)
        {
            const Deliver<ImageReservation> Current = TargetEdge->Resolve(Colour);

            if (!Current.Resolved)
            {
                Rejected = true;
                break;
            }

            Covered.push_back(Current.Resolve().WholeView);
        }

        if (!Rejected && Held.DepthTarget != DepthTargetAbsent)
        {
            const Deliver<ImageReservation> Current =
                TargetEdge->Resolve(static_cast<SharedTarget>(Held.DepthTarget));

            if (!Current.Resolved)
                Rejected = true;
            else
                Covered.push_back(Current.Resolve().WholeView);
        }

        VkFramebuffer Spanned = VK_NULL_HANDLE;

        if (!Rejected)
        {
            VkFramebufferCreateInfo Declaration = {};
            Declaration.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            Declaration.renderPass              = Held.RenderConstruct;
            Declaration.attachmentCount         = static_cast<std::uint32_t>(Covered.size());
            Declaration.pAttachments            = Covered.data();
            Declaration.width                   = DisplayWidth;
            Declaration.height                  = DisplayHeight;
            Declaration.layers                  = 1u;

            if (vkCreateFramebuffer(DeviceEdge->ActiveDevice(), &Declaration, nullptr, &Spanned) != VK_SUCCESS)
                Rejected = true;
        }

        if (Rejected)
        {
            for (const VkFramebuffer Reclaimed : Incoming)
                vkDestroyFramebuffer(DeviceEdge->ActiveDevice(), Reclaimed, nullptr);

            return Deliver<bool>::Refuse(
                { RefusalReason::HostDenied, "a render construct could not be covered at this extent" });
        }

        Incoming.push_back(Spanned);
    }

    Release();

    for (std::size_t ConstructIndex = 0u; ConstructIndex < Constructs.size(); ++ConstructIndex)
        Constructs[ConstructIndex].SpannedTargets = Incoming[ConstructIndex];

    DerivedWidth  = DisplayWidth;
    DerivedHeight = DisplayHeight;
    SpanCurrent  = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<ConstructedSpan> AttachmentIndex::Resolve(std::uint32_t ConstructIndex) const
{
    if (static_cast<std::size_t>(ConstructIndex) >= Constructs.size())
    {
        return Deliver<ConstructedSpan>::Refuse(
            { RefusalReason::ContentUnsupported, "no render construct stands at that ordinal" });
    }

    if (!SpanCurrent)
    {
        return Deliver<ConstructedSpan>::Refuse(
            { RefusalReason::ExtentExhausted, "no span has been derived at any display extent" });
    }

    const HeldConstruct& Held = Constructs[ConstructIndex];

    ConstructedSpan Resolved;
    Resolved.RenderConstruct = Held.RenderConstruct;
    Resolved.SpannedTargets  = Held.SpannedTargets;
    Resolved.SpannedWidth    = DerivedWidth;
    Resolved.SpannedHeight   = DerivedHeight;

    return Deliver<ConstructedSpan>::Result(Resolved);
}

Deliver<VkRenderPass> AttachmentIndex::ConstructOf(std::uint32_t ConstructIndex) const
{
    if (static_cast<std::size_t>(ConstructIndex) >= Constructs.size())
    {
        return Deliver<VkRenderPass>::Refuse(
            { RefusalReason::ContentUnsupported, "no render construct stands at that ordinal" });
    }

    return Deliver<VkRenderPass>::Result(Constructs[ConstructIndex].RenderConstruct);
}

std::uint32_t AttachmentIndex::DeclaredCount() const
{
    return static_cast<std::uint32_t>(Constructs.size());
}

bool AttachmentIndex::SpansDerived() const
{
    return SpanCurrent;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void AttachmentIndex::Release()
{
    if (DeviceEdge == nullptr || DeviceEdge->ActiveDevice() == VK_NULL_HANDLE)
        return;

    for (HeldConstruct& Held : Constructs)
    {
        if (Held.SpannedTargets == VK_NULL_HANDLE)
            continue;

        vkDestroyFramebuffer(DeviceEdge->ActiveDevice(), Held.SpannedTargets, nullptr);

        Held.SpannedTargets = VK_NULL_HANDLE;
    }

    DerivedWidth  = 0u;
    DerivedHeight = 0u;
    SpanCurrent  = false;
}

void AttachmentIndex::Reclaim()
{
    Release();

    if (DeviceEdge != nullptr && DeviceEdge->ActiveDevice() != VK_NULL_HANDLE)
    {
        for (HeldConstruct& Held : Constructs)
        {
            if (Held.RenderConstruct == VK_NULL_HANDLE)
                continue;

            vkDestroyRenderPass(DeviceEdge->ActiveDevice(), Held.RenderConstruct, nullptr);

            Held.RenderConstruct = VK_NULL_HANDLE;
        }
    }

    Constructs.clear();
}

AttachmentIndex::~AttachmentIndex()
{
    Reclaim();
}

}   // namespace Slate
