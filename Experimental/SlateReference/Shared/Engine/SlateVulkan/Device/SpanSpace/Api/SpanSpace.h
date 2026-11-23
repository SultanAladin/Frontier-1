//============================================================================================================================================
//                                                                SPANSPACE.H
//============================================================================================================================================
// 🧩 Device linear extents, each sliced out of ByteSpace and each declaring what the device is permitted to read it as.

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
//                                                  WHAT A SPAN CARRIES
//------------------------------------------------------------------------------------------------------------------------

// 📝 No span; never a valid span ordinal. Sibling of `ByteSpace`'s `AbsentExtent` and `ImageSpace`'s `AbsentImage`.
inline constexpr std::uint32_t AbsentSpan = 0xFFFFFFFFu;   // [-] - the claim names no span

/// 🧩 What the device is permitted to read one span as, which the vendor requires at its creation.
/// note  🔴 Declared and never inferred. The vendor fixes the permitted reads when the span is created, and a
///       span created without the indirect read is one the recording meets as a validation error at the draw —
///       four calls and one ordering away from the declaration that omitted it.
/// note  ⚠️ Every intent below accepts a transfer into the span, because a device-local span is written no other
///       way. The transfer **out** is declared only where something reads it back, since `06` §3 sizes the
///       host-writable extent against what is staged rather than against what is claimed.
/// tag   guarantee
enum class SpanIntent : std::uint32_t
{
    StorageRead    = 0u,   // [-] - read by a shader through a declared descriptor slot
    StorageWritten = 1u,   // [-] - read and written by a shader, and read back
    UniformRead    = 2u,   // [-] - one uniform block, read by every invocation
    IndirectRecord = 3u,   // [-] - the device writes the draw's own ordinals into it
    TransferSource = 4u,   // [-] - host-written staging; the source of a transfer and nothing else
    IntentCount    = 5u    // [-] - the closed count, never an intent
};

/// 🧩 The shape one span is claimed at — how far it runs, what reads it, and where it lives.
/// note  ⚠️ The residency is declared rather than derived from the intent, because a `StorageRead` span is
///       device-local for `16`'s partitioning and host-writable for a per-slot uniform, and the two claim
///       sites know which they are while a table keyed on the intent could not.
/// tag   nonallocating, nonthrowing
struct SpanShape
{
    VkDeviceSize     SpanBytes = 0u;                             // [B] - how far the span runs
    SpanIntent       Intent    = SpanIntent::StorageRead;        // [-] - what the device may read it as
    ExtentResidency  Residency = ExtentResidency::DeviceLocal;   // [-] - device-local, or host-writable
};

/// 🧩 One claimed span — the vendor object, how far it runs, and where the host may write it.
/// note  🔴 `HostAddress` is null for every device-local claim, and that is the distinction rather than a
///        convenience. A caller writing through it unconditionally has written through a null address on the
///        residency that carries the working set, which is every residency but staging.
/// tag   nonallocating, nonthrowing
struct SpanReservation
{
    VkBuffer       Extent      = VK_NULL_HANDLE;   // [-] - the vendor span; the vendor spelling
    VkDeviceSize   SpanBytes   = 0u;               // [B] - as claimed, never as re-queried
    void*          HostAddress = nullptr;          // [-] - null for every DeviceLocal claim
    std::uint32_t  SpanIndex = AbsentSpan;       // [-] - which slot Release returns it to
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SPAN INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every linear device extent the engine holds, each sliced out of `ByteSpace` and resolved by its ordinal.
/// note  🔴 The sibling of `ImageSpace` and deliberately its shape. `ByteSpace` owns bytes and knows nothing of
///        what occupies them; `ImageSpace` occupies them with images and this occupies them with spans. Until
///        this stood, `16` §4's "positions and ordinals read from declared spans" named a vendor object nothing
///        in the engine could construct.
/// note  ⚠️ A released slot is reused rather than erased, so an ordinal a contributing document recorded stays
///        the ordinal it recorded. Erasing would renumber every claim above the released one, and the document
///        holding the ordinal has no way to observe that it was renumbered.
/// tag   owning
class SpanSpace
{
public:

    SpanSpace()                            = default;
    SpanSpace(const SpanSpace&)            = delete;
    SpanSpace& operator=(const SpanSpace&) = delete;
    ~SpanSpace();

    /// 🧩 Takes the device and the byte extents every claimed span is sliced from.
    /// in    Exchange      [-]  the created device; borrowed, never owned, and outlives this component
    /// in    BackingSpace  [-]  where span bytes come from; borrowed and outlives this component
    /// in    Naming        [-]  names every claimed span; borrowed and outlives this component
    /// out   Result       [-]  refuses with CapabilityAbsent when no device is active
    /// note  🔴 `06` §7's diagnostic-name gate. Each span is named by its intent and its ordinal, which is
    ///        what a claimant resolves it by — a name carrying only the intent would be shared by every
    ///        storage span the engine holds, and the driver's text would then name a set rather than a span.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ConstructSpanSpace(const VulkanExchange&      Exchange,
                            ByteSpace&                 BackingSpace,
                            const DiagnosticExtension& Naming);

    /// 🧩 Reservations one span of the declared shape and binds the bytes it occupies.
    /// in    Declared  [-]  the shape; nothing about it is inferred from the intent but its permitted reads
    /// out   Result   [-]  refuses with ExtentExhausted when no bytes remain, and with ContentUnsupported for
    ///                      a zero span or an intent outside the declared set
    /// post  the span stands and is written by a transfer, or through its host address where it carries one
    /// note  🔴 Rejected in full. A span whose bytes were claimed and whose binding was rejected leaves a vendor
    ///        allocation nothing holds a reference to, reclaimed only at device teardown.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<SpanReservation> Reserve(const SpanShape& Declared);

    /// 🧩 Writes host-supplied bytes into one host-writable span.
    /// in    SpanIndex    [-]  a claim this component registered
    /// in    Incoming       [-]  what is written; read for IncomingBytes and never retained
    /// in    IncomingBytes  [B]  how far the write runs
    /// in    ByteOffset     [B]  where in the span the write begins
    /// out   Result        [-]  refuses with ContentUnsupported for an unclaimed ordinal or a device-local
    ///                           span, and with ExtentExhausted when the write would run past the claim
    /// note  🔴 The span is host-coherent, so nothing is flushed here. `ByteSpace` claims the host-writable
    ///        extent as coherent precisely so that a caller cannot forget the flush at one of its write sites.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Amend(std::uint32_t  SpanIndex,
                        const void*    Incoming,
                        VkDeviceSize   IncomingBytes,
                        VkDeviceSize   ByteOffset);

    /// 🧩 Records the transfer that carries one span's bytes into another.
    /// in    Recorded      [-]  the recording being written into
    /// in    SourceIndex [-]  a claim this component registered, declared as a transfer source
    /// in    TargetIndex [-]  a claim this component registered
    /// in    TransferBytes [B]  how far the transfer runs; zero reads as the whole of the source
    /// out   Result       [-]  refuses with ContentUnsupported for an unclaimed ordinal and with
    ///                          ExtentExhausted when the transfer would run past either span
    /// note  ⚠️ The barrier that makes the transferred bytes readable is the caller's, because only the caller
    ///        knows which stage reads them. A transfer recorded without one is read by the device at whatever
    ///        moment its scheduling reaches it, which is a partitioning that is correct on one driver.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Transfer(VkCommandBuffer  Recorded,
                           std::uint32_t    SourceIndex,
                           std::uint32_t    TargetIndex,
                           VkDeviceSize     TransferBytes);

    /// 🧩 The current record for one claimed span.
    /// in    SpanIndex  [-]  a claim this component registered
    /// out   Result      [-]  refuses with ContentUnsupported for an unclaimed ordinal
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<SpanReservation> Current(std::uint32_t SpanIndex) const;

    /// 🧩 Destroys one span and returns its bytes.
    /// in    SpanIndex  [-]  a claim this component registered; an unclaimed ordinal is a no-op
    /// pre   the device is idle, or no recording still in the rotation reads it
    /// cost  🚩
    /// tag   api, nonthrowing
    void Release(std::uint32_t SpanIndex);

    /// 🧩 Releases every claimed span.
    /// pre   the device is idle
    /// cost  🔴
    /// tag   api, nonthrowing
    void Reclaim();

    std::uint32_t ReservedCount() const;
    VkDeviceSize  ReservedBytes() const;

private:

    struct HeldSpan
    {
        VkBuffer      Extent       = VK_NULL_HANDLE;          // [-] - the vendor span
        ByteReservation     Backing      = {};                      // [-] - the bytes it occupies
        SpanShape     Shape        = {};                      // [-] - as claimed
        bool          SlotOccupied = false;                   // [-] - false once released
    };

    /// 🧩 What the declared intent permits the device to read a span as, as the vendor spells it.
    /// in    Intent  [-]  the declared intent
    /// out   Usage   [-]  the vendor flags, transfer destination included
    static VkBufferUsageFlags UsageOf(SpanIntent Intent);

    /// 🧩 What one declared intent is named in the driver's text, so a claim's name states what reads it.
    static const char* NameOf(SpanIntent Intent);

    const VulkanExchange*       DeviceEdge   = nullptr;   // [-] - borrowed; never owned
    ByteSpace*                  BackingBytes = nullptr;   // [-] - borrowed; never owned
    const DiagnosticExtension*  NamingEdge   = nullptr;   // [-] - borrowed; never owned
    std::vector<HeldSpan>       Spans        = {};        // [-] - released slots are reused, never erased
};

}   // namespace Slate
