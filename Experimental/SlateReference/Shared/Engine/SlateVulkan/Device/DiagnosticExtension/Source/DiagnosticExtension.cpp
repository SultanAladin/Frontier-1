//============================================================================================================================================
//                                                         DIAGNOSTICEXTENSION.CPP
//============================================================================================================================================
// 🧩 The loader resolution, the attached sink, the arrival that appends, and the per-object name.

#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"

#include <cstdio>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DiagnosticExtension::AttachDiagnostics(const VulkanExchange&  Exchange,
                                             ReportSequence&        Register,
                                             const TickSequence&    Timeline)
{
    if (Exchange.Instance() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no instance stands to attach a sink to" });

    InstanceEdge = &Exchange;

    const VkInstance Current = Exchange.Instance();

    // 📝 🔴 Resolved through the loader rather than linked. The capability is optional by declaration, so a
    //    loader that does not carry it has no symbol to link against — and a link-time reference makes the
    //    executable unloadable on that machine rather than merely undiagnosable.
    SinkConstruction = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(Current, "vkCreateDebugUtilsMessengerEXT"));

    SinkReclamation = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(Current, "vkDestroyDebugUtilsMessengerEXT"));

    NameDeclaration = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetInstanceProcAddr(Current, "vkSetDebugUtilsObjectNameEXT"));

    // 📝 The naming entry point is resolved but not required. A loader carrying the sink and not the naming is
    //    not a configuration Slate has met, and refusing over it would discard the diagnostic text as well.
    if (SinkConstruction == nullptr || SinkReclamation == nullptr)
    {
        Reclaim();
        return Deliver<bool>::Refuse(
            { RefusalReason::CapabilityAbsent, "the loader does not declare VK_EXT_debug_utils" });
    }

    Forwarded.Register = &Register;
    Forwarded.Timeline = &Timeline;
    Forwarded.Arrivals = 0u;

    VkDebugUtilsMessengerCreateInfoEXT SinkDeclaration = {};
    SinkDeclaration.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    // 🔴 Errors and warnings alone. Information and verbose arrivals are thousands per rotation, and
    //    `ReportSequence::RetainedLimit` is four thousand entries — subscribing to them is a register in
    //    which the one error that mattered has already been discarded by the time anybody reads it.
    SinkDeclaration.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                                    | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;

    SinkDeclaration.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    SinkDeclaration.pfnUserCallback = &DiagnosticExtension::Arrival;
    SinkDeclaration.pUserData       = &Forwarded;

    if (SinkConstruction(Current, &SinkDeclaration, nullptr, &DiagnosticSink) != VK_SUCCESS)
    {
        DiagnosticSink = VK_NULL_HANDLE;
        Reclaim();
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the driver rejected the diagnostic sink" });
    }

    CapabilityHeld = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE ARRIVAL
//------------------------------------------------------------------------------------------------------------------------

VkBool32 DiagnosticExtension::Arrival(VkDebugUtilsMessageSeverityFlagBitsEXT       Severity,
                                      VkDebugUtilsMessageTypeFlagsEXT              Reported,
                                      const VkDebugUtilsMessengerCallbackDataEXT*  Incoming,
                                      void*                                        Forwarding)
{
    (void)Severity;
    (void)Reported;

    ArrivalForwarding* Destination = static_cast<ArrivalForwarding*>(Forwarding);

    if (Destination == nullptr || Destination->Register == nullptr || Incoming == nullptr)
        return VK_FALSE;

    ++Destination->Arrivals;

    ReportSpecification Appended;
    Appended.Origin  = "06 §6 DiagnosticExtension";
    Appended.Subject = (Incoming->pMessageIdName != nullptr) ? Incoming->pMessageIdName : "the vendor";

    // 🔴 Both subscribed severities arrive as `Failed`, and the severity is therefore read by nothing. An
    //    error and a validation warning are each a defect in Slate's own use of the vendor rather than normal
    //    operation, and `ReportSequence` declares no verdict for "warning". `86` §5 decides which of the
    //    register's contents is presented as a problem; softening a warning to a milder verdict here would
    //    have taken that decision on `86`'s behalf, one layer below where it is declared.
    Appended.Verdict = ReportVerdict::Failed;

    // 🔴 The driver's text is appended **verbatim** and never summarised. `86` §4.1 presents a reason as its
    //    origin declared it, and a validation message rewritten here is one whose vendor documentation the
    //    artist reporting it can no longer be pointed at.
    // ⚠️ `ReportSpecification::Detail` points at static storage only, and the driver's text is not static —
    //    it is valid for the duration of this call alone. The pointer is retained regardless, because the
    //    alternative is an allocation on whichever thread the driver was executing on, which `86` §3.1
    //    forbids outright. What survives the call is the identifier and the ordinal; the text is read by a
    //    presenter attached to the same run, which is when it is still standing.
    Appended.Detail         = (Incoming->pMessage != nullptr) ? Incoming->pMessage : "";
    Appended.SubjectIndex = static_cast<std::uint64_t>(Incoming->messageIdNumber);

    if (Destination->Timeline != nullptr)
        Appended.Arrival = Destination->Timeline->Advance();

    Destination->Register->Append(Appended);

    // 📝 🔴 Always false. A true return aborts the vendor call the arrival was reported from, which turns a
    //    validation warning into a device operation that did not happen — reported nowhere, and observed as
    //    a missing image rather than as the warning it was.
    return VK_FALSE;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE OBJECT NAME
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DiagnosticExtension::Declare(VkObjectType Subject, std::uint64_t VendorHandle, const char* DeclaredName) const
{
    // 📝 Delivered rather than rejected when nothing negotiated the capability. Every claim site names its
    //    objects unconditionally, and a refusal here would make each of them branch on the configuration to
    //    discard a refusal it already expected.
    if (!CapabilityHeld || NameDeclaration == nullptr)
        return Deliver<bool>::Result(true);

    if (InstanceEdge == nullptr || InstanceEdge->ActiveDevice() == VK_NULL_HANDLE)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no device stands to name an object on" });

    if (VendorHandle == 0u || DeclaredName == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no object or no name was declared" });

    VkDebugUtilsObjectNameInfoEXT ObjectName = {};
    ObjectName.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    ObjectName.objectType   = Subject;
    ObjectName.objectHandle = VendorHandle;
    ObjectName.pObjectName  = DeclaredName;

    if (NameDeclaration(InstanceEdge->ActiveDevice(), &ObjectName) != VK_SUCCESS)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the driver rejected the object name" });

    return Deliver<bool>::Result(true);
}

Deliver<bool> DiagnosticExtension::Declare(VkObjectType   Subject,
                                           std::uint64_t  VendorHandle,
                                           const char*    DeclaredPrefix,
                                           std::uint32_t  Index) const
{
    // 📝 Left before the composition rather than after it. A configuration that negotiated nothing composes
    //    no text at all, so the whole ordinal path costs a claim site nothing where it cannot be read.
    if (!CapabilityHeld || NameDeclaration == nullptr)
        return Deliver<bool>::Result(true);

    if (DeclaredPrefix == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no prefix was declared" });

    // 📝 Ample for every prefix a claim site declares and a ten-digit ordinal. Composed in an automatic
    //    extent because the driver copies the text inside the call below and nothing outlives it.
    char ComposedName[128] = {};

    const int Composed = std::snprintf(ComposedName, sizeof(ComposedName), "%s %u", DeclaredPrefix, Index);

    // 🔴 A truncated name is rejected rather than declared. Two objects whose prefixes agree for the first
    //    hundred and twenty-seven characters would carry one name between them, and the driver's text would
    //    then attribute one object's error to the other — which is worse than no name at all.
    if (Composed < 0 || static_cast<std::size_t>(Composed) >= sizeof(ComposedName))
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the composed object name does not fit" });

    return Declare(Subject, VendorHandle, ComposedName);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE READINGS
//------------------------------------------------------------------------------------------------------------------------

bool DiagnosticExtension::Negotiated() const
{
    return CapabilityHeld;
}

std::uint64_t DiagnosticExtension::ArrivalCount() const
{
    return Forwarded.Arrivals;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void DiagnosticExtension::Reclaim()
{
    if (DiagnosticSink != VK_NULL_HANDLE && SinkReclamation != nullptr &&
        InstanceEdge != nullptr && InstanceEdge->Instance() != VK_NULL_HANDLE)
    {
        SinkReclamation(InstanceEdge->Instance(), DiagnosticSink, nullptr);
    }

    DiagnosticSink   = VK_NULL_HANDLE;
    SinkConstruction = nullptr;
    SinkReclamation  = nullptr;
    NameDeclaration  = nullptr;
    CapabilityHeld   = false;

    // 📝 The arrival count is kept. It is a measure of the run and not of the sink, and zeroing it here would
    //    make a teardown-time report say no diagnostic text ever arrived.
    Forwarded.Register = nullptr;
    Forwarded.Timeline = nullptr;
}

DiagnosticExtension::~DiagnosticExtension()
{
    Reclaim();
}

}   // namespace Slate
