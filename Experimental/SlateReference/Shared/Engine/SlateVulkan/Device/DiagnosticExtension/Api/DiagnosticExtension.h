//============================================================================================================================================
//                                                          DIAGNOSTICEXTENSION.H
//============================================================================================================================================
// 🧩 `VK_EXT_debug_utils` — queried, enabled and held, so every device object Slate creates carries a name.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateMath/Platform/TickSequence/Api/TickSequence.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                              THE NEGOTIATED CAPABILITY
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The one optional vendor capability `06` negotiates by name — the driver's own diagnostic text, and the
///    per-object naming that makes that text nameable.
/// note  🔴 This is `SKILL-Naming`'s narrow reading of `Extension`: a queried, enabled and **owned** optional
///        vendor capability. `VulkanExchange::ConstructInstance` requests the extension and the validation
///        layer; nothing there holds what the request produced, and this holds it.
/// note  🔴 Compiled in every configuration rather than behind `SLATE_DEBUG`. A component compiled out of
///        Release is one whose every call site is conditional, and `06` §1's note on the capability set applies
///        verbatim — those conditionals never all leave. `Construct` refuses instead when nothing negotiated it,
///        and `Declare` delivers as a no-op, so no call site branches on the configuration.
/// note  ⚠️ Attached **after** `ConstructInstance` and **before** `ConstructDevice`. The capability is
///        instance-level, and a sink attached after the device exists cannot report what device creation itself
///        rejected — which is the one message worth having on a machine where bring-up refuses.
/// tag   owning
class DiagnosticExtension
{
public:

    DiagnosticExtension()                                      = default;
    DiagnosticExtension(const DiagnosticExtension&)            = delete;
    DiagnosticExtension& operator=(const DiagnosticExtension&) = delete;
    ~DiagnosticExtension();

    /// 🧩 Resolves the capability's entry points and attaches the sink the driver writes its diagnostic text into.
    /// in    Exchange  [-]  the constructed instance; borrowed and outlives this component
    /// in    Register  [-]  where incoming driver text is appended; borrowed and outlives this component
    /// in    Timeline  [-]  stamps each arrival where it happened; borrowed and outlives this component
    /// out   Result   [-]  refuses with CapabilityAbsent when no instance stands or the loader does not
    ///                      declare the capability, and with HostDenied when the driver declines the sink
    /// pre   `VulkanExchange::ConstructInstance` delivered with the diagnostic requested
    /// post  driver text arrives at `Register` until Reclaim
    /// note  🔴 The entry points are resolved through the loader rather than linked. `VK_EXT_debug_utils` is
    ///        optional by declaration, so its symbols are absent from the import library on a machine whose
    ///        loader does not carry it — and a link-time reference makes the whole executable unloadable there.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> AttachDiagnostics(const VulkanExchange& Exchange, ReportSequence& Register, const TickSequence& Timeline);

    /// 🧩 Names one vendor object, so the driver's text names the object rather than an address.
    /// in    Subject       [-]  which vendor structure the handle names, as the vendor spells it
    /// in    VendorHandle  [-]  the object, widened to the vendor's own naming width
    /// in    DeclaredName  [-]  static text; the driver copies it and nothing here retains it
    /// out   Result       [-]  refuses with CapabilityAbsent when no device stands, and with HostDenied when
    ///                          the driver declines the name
    /// note  🔴 `06` §7's gate — every device object Slate creates carries a diagnostic name in Debug — is
    ///        discharged at each **claim site**, not here. This is the one mechanism that can name an object;
    ///        the gate is met only once `ByteSpace`, `ImageSpace`, `SpanSpace`, `DescriptorIndex`,
    ///        `ProgramIndex`, `CommandSequence`, `CycleScheduler` and `DisplayScheduler` each call it.
    /// note  ⚠️ Delivers as a no-op when nothing negotiated the capability. A name declared in a configuration
    ///        with no diagnostic capability is not a failure, and refusing would make every claim site branch
    ///        on the configuration to ignore a refusal it expected.
    /// note  📝 `VendorHandle` spells the banned structural word deliberately, under `SKILL-Naming`'s
    ///        third-party exemption and beside the existing `WindowExchange::Convert(…, void* NativeHandle)`.
    ///        The vendor's own field is `objectHandle` and one spelling at the edge is what the exemption is for.
    /// note  📝 Const because it names an object the caller owns and amends nothing of its own. A claim site
    ///        holds this as a borrowed const edge beside the device, and a non-const one would make every such
    ///        component hold a mutable reference to a capability it only reads.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(VkObjectType Subject, std::uint64_t VendorHandle, const char* DeclaredName) const;

    /// 🧩 Names one vendor object by a static prefix and the ordinal its owning component holds it at.
    /// in    Subject        [-]  which vendor structure the handle names, as the vendor spells it
    /// in    VendorHandle   [-]  the object, widened to the vendor's own naming width
    /// in    DeclaredPrefix [-]  static text naming what the object is; the ordinal is appended to it
    /// in    Index        [-]  the slot the owning component resolves the object by
    /// out   Result        [-]  refuses as the two-operand form does, and with ContentUnsupported when the
    ///                           composed text does not fit the extent it is composed in
    /// note  🔴 The composition is here rather than at each claim site. Eight components name objects by their
    ///        own ordinal, and eight separate compositions is eight places where one of them formats the
    ///        ordinal differently and the driver's text stops sorting alongside the rest.
    /// note  ⚠️ Composed into an automatic extent and read by the driver before this returns, which is what
    ///        the two-operand form's guarantee already accepts — the driver copies the text and nothing is
    ///        retained here.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Declare(VkObjectType   Subject,
                          std::uint64_t  VendorHandle,
                          const char*    DeclaredPrefix,
                          std::uint32_t  Index) const;

    /// 🧩 Whether the capability was negotiated, for a claim site reporting what it could not name.
    /// out   Negotiated  [-]  false in every configuration that did not request the diagnostic
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Negotiated() const;

    /// 🧩 How many diagnostic arrivals the driver has reported since the sink attached.
    /// note  Counted here as well as appended, because `ReportSequence` coalesces recurrences and the raw
    ///        arrival count is what says whether a quiet register means a clean run or a detached sink.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t ArrivalCount() const;

    /// 🧩 Detaches the sink and forgets every resolved entry point.
    /// pre   nothing is still recording; the instance stands until after this returns
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

private:

    /// 🧩 The C-ABI arrival the driver calls, which forwards to the register the construction was given.
    /// note  🔴 `86` §3.1 accepts an append from any thread and this arrives on whichever thread the driver
    ///        was executing on. Nothing here allocates and nothing here takes a lock the driver does not
    ///        already hold — the register's own guard is the only one.
    /// note  🔴 Every arrival is appended as `Failed`, so the severity is read by nothing. `ReportSequence`
    ///        declares seven dispositions and none of them is "warning": an error and a validation warning are
    ///        each a defect in Slate's own use of the vendor rather than normal operation, and `86` §5 — not a
    ///        mapping here — decides what is presented as a problem. Only those two severities are subscribed
    ///        to; a bounded register filled with information arrivals is one in which the error that mattered
    ///        has already been discarded.
    static VKAPI_ATTR VkBool32 VKAPI_CALL Arrival(VkDebugUtilsMessageSeverityFlagBitsEXT       Severity,
                                                  VkDebugUtilsMessageTypeFlagsEXT              Reported,
                                                  const VkDebugUtilsMessengerCallbackDataEXT*  Incoming,
                                                  void*                                        Forwarding);

    // 📝 Forwarded to the C-ABI arrival through the driver's own user pointer, so the arrival reaches the
    //    register without a translation unit-scope pointer that a second instance would overwrite.
    struct ArrivalForwarding
    {
        ReportSequence*      Register    = nullptr;   // [-] - borrowed; never owned
        const TickSequence*  Timeline    = nullptr;   // [-] - borrowed; never owned
        std::uint64_t        Arrivals    = 0u;        // [-] - raised by every arrival, coalesced or not
    };

    const VulkanExchange*              InstanceEdge      = nullptr;         // [-] - borrowed; never owned
    VkDebugUtilsMessengerEXT           DiagnosticSink    = VK_NULL_HANDLE;  // [-] - where driver text drains to
    PFN_vkCreateDebugUtilsMessengerEXT SinkConstruction  = nullptr;         // [-] - resolved through the loader
    PFN_vkDestroyDebugUtilsMessengerEXT SinkReclamation  = nullptr;         // [-] - resolved through the loader
    PFN_vkSetDebugUtilsObjectNameEXT   NameDeclaration   = nullptr;         // [-] - resolved through the loader
    ArrivalForwarding                  Forwarded         = {};              // [-] - handed to the driver by address
    bool                               CapabilityHeld    = false;           // [-] - the sink stands
};

}   // namespace Slate
