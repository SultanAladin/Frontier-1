//============================================================================================================================================
//                                                               BYTESPACE.H
//============================================================================================================================================
// 🧩 Raw device byte extents, claimed in a few large pieces and sliced into the spans every resource sits in.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateVulkan/Device/DiagnosticExtension/Api/DiagnosticExtension.h"
#include "SlateVulkan/Device/VulkanExchange/Api/VulkanExchange.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   EXTENT RESIDENCY
//------------------------------------------------------------------------------------------------------------------------

// 📝 No extent; never a valid extent ordinal. Declared per unit, matching `20`'s `AbsentTile` — nothing reads
//    two of them, and a shared spelling would be a dependency edge no traversal can see.
inline constexpr std::uint32_t AbsentExtent = 0xFFFFFFFFu;   // [-] - the claim names no sliced extent

/// 🧩 Where a claimed span lives, which is the only distinction the caller makes.
/// note  🔴 The vendor's `memoryTypeIndex` is **not** this. It is resolved from this by scoring what the
///       device declares, and it differs per device, per driver and per configuration. A caller naming the
///       vendor ordinal directly has hard-coded one machine's declaration into a claim site.
/// tag   guarantee
enum class ExtentResidency : std::uint32_t
{
    DeviceLocal    = 0u,   // [-] - fastest for the device; unreachable from the host
    HostWritable   = 1u,   // [-] - host-visible and coherent; staging content and per-slot uniforms
    ResidencyCount = 2u    // [-] - the closed count, never a residency
};

/// 🧩 What the claimant promises about the span, and therefore what exhaustion means for it.
/// note  🔴 `06` §7: a reserved or committed claim is rejected **before** partial success, and discretionary
///       exhaustion is residency policy rather than a reported failure. The distinction cannot be derived
///       from the span — a hundred megabytes is the working set for one caller and an optional prefetch for
///       the next — so it is declared at the claim and carried into the refusal.
/// tag   guarantee
enum class ReservationCondition : std::uint32_t
{
    Reserved      = 0u,   // [-] - taken at bring-up and held for the run; exhaustion ends bring-up
    Committed     = 1u,   // [-] - the working set; exhaustion is a reported failure
    Discretionary = 2u    // [-] - evictable residency; exhaustion is `20`'s policy operating as designed
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE CLAIMED SPAN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One sliced byte span — where it sits, how far it runs, and which extent it came out of.
/// note  A default-constructed claim names `AbsentExtent` and is what a refusal leaves behind. Releasing one
///       is a no-op, which is what makes the caller's reclamation path unconditional.
/// tag   nonallocating, nonthrowing
struct ByteReservation
{
    VkDeviceMemory  BackingExtent = VK_NULL_HANDLE;   // [-] - the vendor allocation the span was sliced from
    VkDeviceSize    ByteOffset    = 0u;               // [B] - where the span begins inside that allocation
    VkDeviceSize    ByteSpan      = 0u;               // [B] - how far it runs, alignment padding excluded
    void*           HostAddress   = nullptr;          // [-] - null for every DeviceLocal claim
    std::uint32_t   ExtentIndex = AbsentExtent;     // [-] - which sliced extent Release returns it to
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE SLICED EXTENTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 `06` §2.1 settles allocation as sub-allocation from large device extents. One vendor allocation per
//    resource exhausts `maxMemoryAllocationCount`, which is 4096 on much of the installed hardware and is
//    reached by a document with a few thousand owners — long before any memory budget is.
inline constexpr VkDeviceSize DeviceLocalExtentBytes  = 256ull * 1024ull * 1024ull;   // [B] - one device-local piece
inline constexpr VkDeviceSize HostWritableExtentBytes = 64ull  * 1024ull * 1024ull;   // [B] - one host-writable piece

/// 🧩 Every device byte the engine holds, sliced from a small number of large vendor allocations.
/// note  🔴 This owns bytes and knows nothing of what occupies them. An image is `ImageSpace`'s and a vertex
///       span is `16`'s; both arrive here as a span and an alignment and leave as an offset.
/// note  ⚠️ Reclamation is whole-extent. A released span returns to its extent's free list and is coalesced
///       with its neighbours, but no extent is ever handed back to the vendor until Reclaim — an extent
///       returned while any claim still stands is a use-after-free the validation layer reports somewhere
///       else entirely.
/// tag   owning
class ByteSpace
{
public:

    ByteSpace()                            = default;
    ByteSpace(const ByteSpace&)            = delete;
    ByteSpace& operator=(const ByteSpace&) = delete;
    ~ByteSpace();

    /// 🧩 Takes the device and reads the vendor declaration every later claim is scored against.
    /// in    Exchange  [-]  the created device; borrowed, never owned, and outlives this component
    /// in    Naming    [-]  names every vendor allocation taken; borrowed and outlives this component
    /// out   Result   [-]  refuses with CapabilityAbsent when no device is active
    /// post  no extent is claimed; the first Reserve takes the first one
    /// note  🔴 `06` §7's diagnostic-name gate is discharged here rather than by the caller. A name declared
    ///        by whoever happened to call `ConstructExtent` would be absent for the extents this component
    ///        takes on its own, which is every extent after the first of each residency.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ReserveByteSpace(const VulkanExchange& Exchange, const DiagnosticExtension& Naming);

    /// 🧩 Slices one span of the requested residency, taking a further extent when none can satisfy it.
    /// in    RequestedBytes  [B]  how far the span must run
    /// in    ByteAlignment   [B]  the alignment the vendor declared for what occupies it; zero reads as one
    /// in    Residency       [-]  device-local or host-writable
    /// in    Condition        [-]  what exhaustion means for this claimant
    /// out   Result         [-]  refuses with ExtentExhausted, in full, with nothing partially claimed
    /// err   refuses with ContentUnsupported for a zero span or an alignment that is not a power of two
    /// note  🔴 The refusal is whole. A claim that half-succeeded would leave the caller holding a span it
    ///       cannot use and cannot release, and the release path is the one nobody exercises.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<ByteReservation> Reserve(VkDeviceSize    RequestedBytes,
                             VkDeviceSize    ByteAlignment,
                             ExtentResidency Residency,
                             ReservationCondition   Condition);

    /// 🧩 Returns one span to its extent's free list, coalescing it with whatever it now adjoins.
    /// in    Reserved  [-]  a claim this component registered; a default-constructed one is a no-op
    /// post  the span is claimable again immediately
    /// note  ⚠️ Immediately, not after the recording slot count. A span the device may still be reading is
    ///       quarantined by its **owner** — `20` §5 does exactly that over its own slots — because only the
    ///       owner knows which rotation last recorded against it.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Release(const ByteReservation& Reserved);

    /// 🧩 Destroys every vendor allocation and forgets every slice.
    /// pre   the device is idle and no claim this component registered is still recorded against
    /// cost  🚩
    /// tag   api, nonthrowing
    void Reclaim();

    /// 🧩 What is claimed and what is held, per residency — the two halves `86` reports separately.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    VkDeviceSize  ReservedBytes(ExtentResidency Residency) const;
    VkDeviceSize  BackingBytes(ExtentResidency Residency) const;
    std::uint32_t ExtentCount() const;

private:

    // 📝 One free span inside one extent. Ordered by offset so that coalescing is a comparison against the
    //    two neighbours rather than a scan, and so that a claim takes the first span that fits.
    struct FreeSpan
    {
        VkDeviceSize  ByteOffset = 0u;   // [B]
        VkDeviceSize  ByteSpan   = 0u;   // [B]
    };

    struct SlicedExtent
    {
        VkDeviceMemory         DeviceExtent  = VK_NULL_HANDLE;                 // [-] - the vendor allocation
        VkDeviceSize           TotalBytes    = 0u;                             // [B] - what it spans
        VkDeviceSize           TakenBytes    = 0u;                             // [B] - claimed out of it
        void*                  HostAddress   = nullptr;                        // [-] - mapped once, never per claim
        ExtentResidency        Residency     = ExtentResidency::DeviceLocal;   // [-]
        std::uint32_t          VendorIndex = 0u;                             // [-] - the memoryTypeIndex scored
        std::vector<FreeSpan>  Unclaimed     = {};                             // [-] - ascending by offset
    };

    /// 🧩 Scores what the device declares for the one entry that satisfies a residency.
    /// out   Result  [-]  refuses with CapabilityAbsent when nothing declared carries the properties
    Deliver<std::uint32_t> ClassifyResidency(ExtentResidency Residency) const;

    /// 🧩 Takes one further vendor allocation, at least as large as the span that could not be satisfied.
    /// out   Result  [-]  refuses with ExtentExhausted when the vendor declines the allocation
    Deliver<std::uint32_t> ConstructExtent(ExtentResidency Residency, VkDeviceSize MinimumBytes);

    const VulkanExchange*             DeviceEdge      = nullptr;   // [-] - borrowed; never owned
    const DiagnosticExtension*        NamingEdge      = nullptr;   // [-] - borrowed; never owned
    VkPhysicalDeviceMemoryProperties  VendorDeclared  = {};        // [-] - vendor spelling; read once
    std::vector<SlicedExtent>         Extents         = {};        // [-] - every vendor allocation held
    VkDeviceSize                      NonCoherentAtom = 1u;        // [B] - floor a host-writable span aligns to
};

}   // namespace Slate
